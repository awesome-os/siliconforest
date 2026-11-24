# SPDX-License-Identifier: MIT
"""Owned-profile window/private-window and real content download controls.

These use browser commands and DOM activation, not physical input. Native seat
shortcuts and native panels are measured separately by native_ui.py.
"""
import hashlib
import json
from pathlib import Path
import time


def measure(client, output, deadline):
    result = dict(synthetic_browser_commands=True, tests=[])

    def save():
        output.write_text(json.dumps(result, indent=2) + '\n')

    def chrome(script, args=None, asynchronous=False):
        client.command('Marionette:SetContext', {'value': 'chrome'})
        return client.command('WebDriver:ExecuteAsyncScript' if asynchronous else 'WebDriver:ExecuteScript',
                              dict(script=script, args=args or [], newSandbox=False, scriptTimeout=12000))

    def check(name, actual, passed):
        result['tests'].append(dict(name=name, actual=actual, passed=bool(passed)))
        save()

    client.command('Marionette:SetContext', {'value': 'content'})
    original = client.command('WebDriver:GetWindowHandles')[0]
    chrome('window.__sffxQualityWindows=[];return true;')
    try:
        for private in [False, True]:
            actual = chrome('''const isPrivate=arguments[0], done=arguments[arguments.length-1];
              const {PrivateBrowsingUtils}=ChromeUtils.importESModule("resource://gre/modules/PrivateBrowsingUtils.sys.mjs");
              const before=[...Services.wm.getEnumerator("navigator:browser")].length;
              const w=window.OpenBrowserWindow({private:isPrivate});window.__sffxQualityWindows.push(w);
              const start=Date.now();const timer=setInterval(()=>{
                if(w.gBrowser&&w.document.readyState==="complete"){
                  clearInterval(timer);done({before,after:[...Services.wm.getEnumerator("navigator:browser")].length,
                    private:PrivateBrowsingUtils.isWindowPrivate(w),ready:w.document.readyState});
                }else if(Date.now()-start>10000){clearInterval(timer);done({error:"new window timeout"});}
              },100);''', [private], True)
            check('private-window' if private else 'second-window', actual,
                  not actual.get('error') and actual['after'] == actual['before'] + 1 and actual['private'] == private)
            chrome('const w=window.__sffxQualityWindows.pop();if(w&&!w.closed)w.close();return true;')
            time.sleep(.5)
    finally:
        chrome('for(const w of window.__sffxQualityWindows||[])if(!w.closed)w.close();window.__sffxQualityWindows=[];return true;')
    client.command('Marionette:SetContext', {'value': 'content'})
    client.command('WebDriver:SwitchToWindow', {'handle': original})
    # The browser's content/download stack writes a new owned path; no internal
    # download API or Python stand-in creates the expected file.
    directory = output.parent / 'downloads'
    directory.mkdir(exist_ok=False)
    filename = 'sf-firefox-download.txt'
    payload = 'SiliconForest Firefox download — exact UTF-8 bytes\n'
    chrome('''Services.prefs.setIntPref("browser.download.folderList",2);
      Services.prefs.setBoolPref("browser.download.useDownloadDir",true);
      Services.prefs.setStringPref("browser.download.dir",arguments[0]);return true;''', [str(directory)])
    client.command('Marionette:SetContext', {'value': 'content'})
    actual = client.command('WebDriver:ExecuteScript', dict(script='''
      const data=arguments[0],name=arguments[1],link=document.createElement("a");
      link.href=URL.createObjectURL(new Blob([data],{type:"text/plain;charset=utf-8"}));
      link.download=name;document.body.append(link);link.click();link.remove();return true;
      ''', args=[payload, filename], newSandbox=False))
    target = directory / filename
    until = min(deadline - 8, time.monotonic() + 15)
    while time.monotonic() < until:
        if target.is_file() and target.stat().st_size == len(payload.encode()) and not target.with_suffix(target.suffix + '.part').exists():
            break
        time.sleep(.2)
    got = target.read_bytes() if target.is_file() else b''
    check('content-download', dict(dom_activation=actual, exists=target.is_file(), bytes=len(got),
                                  sha256=hashlib.sha256(got).hexdigest()), got == payload.encode())
    result['complete'] = bool(result['tests']) and all(row['passed'] for row in result['tests'])
    save()
    return result
