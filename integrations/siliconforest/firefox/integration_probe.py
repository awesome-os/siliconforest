# SPDX-License-Identifier: MIT
"""Qualify the actual launcher and default URL delivery in the owned test home."""
import json
from pathlib import Path
import plistlib
import secrets
import time


def measure(client, output, launcher, bundle, home):
    for value in [launcher, bundle]:
        if not value or not value.startswith('/private/var/db/sffx/') or '..' in Path(value).parts:
            raise ValueError('Integration targets must remain in the private prefix')
    applications = home / 'Applications'
    applications.mkdir(exist_ok=False)
    (applications / 'Firefox.app').symlink_to(bundle)
    preferences = home / 'Library/Preferences'
    preferences.mkdir(parents=True, exist_ok=True)
    destination = preferences / 'com.apple.LaunchServices.plist'
    if destination.exists():
        raise FileExistsError(destination)
    destination.write_bytes(plistlib.dumps(dict(LSHandlers=[
        dict(LSHandlerURLScheme=scheme, LSHandlerRoleAll='org.mozilla.firefox') for scheme in ['http', 'https']])))
    url = 'https://example.com/?sf-firefox-integration=' + secrets.token_hex(6)
    client.command('Marionette:SetContext', {'value': 'chrome'})
    client.command('WebDriver:SetTimeouts', {'script': 30000})
    before = client.command('WebDriver:ExecuteScript', dict(script='''return {
      pid:Components.classes["@mozilla.org/xre/app-info;1"].getService(Components.interfaces.nsIXULRuntime).processID,
      profile:Services.dirsvc.get("ProfD",Components.interfaces.nsIFile).path,
      tabs:[...Services.wm.getEnumerator("navigator:browser")].reduce((n,w)=>n+w.gBrowser.tabs.length,0)};''', args=[], newSandbox=False))
    # A real nsIProcess child keeps the complete delivery process family under
    # this browser's already tracked ownership. It invokes the actual open(1),
    # actual wrapper and Firefox's own CFMessagePort command-line remoting.
    delivery = client.command('WebDriver:ExecuteAsyncScript', dict(script='''
      const path=arguments[0],url=arguments[1],profile=arguments[2],done=arguments[arguments.length-1];
      const file=Components.classes["@mozilla.org/file/local;1"].createInstance(Components.interfaces.nsIFile);
      file.initWithPath(path);
      const process=Components.classes["@mozilla.org/process/util;1"].createInstance(Components.interfaces.nsIProcess);
      process.init(file);window.__sffxDeliveryProcess=process;
      const args=[url,"--args","--profile",profile];
      const observer={observe(subject,topic){let exitValue=null;try{exitValue=process.exitValue;}catch(error){} done({topic,exitValue});}};
      process.runAsync(args,args.length,observer,false);
      ''', args=[launcher, url, before['profile']], newSandbox=False))
    until = time.monotonic() + 8
    after = None
    while time.monotonic() < until:
        after = client.command('WebDriver:ExecuteScript', dict(script='''return {
          pid:Components.classes["@mozilla.org/xre/app-info;1"].getService(Components.interfaces.nsIXULRuntime).processID,
          windows:[...Services.wm.getEnumerator("navigator:browser")].map(w=>({
            tabs:[...w.gBrowser.browsers].map(b=>b.currentURI.spec)}))};''', args=[], newSandbox=False))
        if any(url in window['tabs'] for window in after['windows']):
            break
        time.sleep(.2)
    result = dict(synthetic_process_launch=True, owner_defaults_unchanged=True, isolated_home=str(home),
                  requested_url=url, before=before, delivery=delivery, after=after)
    result['complete'] = bool(delivery.get('topic') == 'process-finished' and delivery.get('exitValue') == 0 and
        after and after['pid'] == before['pid'] and any(url in window['tabs'] for window in after['windows']))
    output.write_text(json.dumps(result, indent=2) + '\n')
    return result
