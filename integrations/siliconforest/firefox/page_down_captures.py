# SPDX-License-Identifier: MIT
"""Collect the installed-browser stale-tile reproducer; pixels need human review.

Five trusted Page Down presses per owned public-page tab, through the explicitly
admitted invisible virtual seat. No load result is a paint correctness verdict.
"""
import json
from pathlib import Path
import time


def measure(client, directory, deadline):
    from presentation_seat import admit, _request
    from renderer_capture import capture
    inject = Path('/private/var/run/gpu-desktop/inject')
    rows = []
    result = dict(complete=False, correctness_qualified=False, visual_review_pending=True,
                  virtual_seat=True, page_down_presses=5, pages=rows)
    output = directory / 'page-down-captures.json'
    def script(source):
        return client.command('WebDriver:ExecuteScript', dict(script=source, args=[], newSandbox=False))
    client.command('Marionette:SetContext', {'value': 'chrome'})
    result['native_compositor'] = script('return Services.prefs.getBoolPref("gfx.webrender.compositor");')
    urls = ['https://www.bbc.com/',
            'https://github.com/torvalds/linux/blob/master/kernel/sched/core.c',
            'https://en.wikipedia.org/wiki/Euler%27s_identity', 'https://www.reddit.com/']
    for expected_url in urls:
        if time.monotonic() > deadline - 12:
            raise RuntimeError('Insufficient time for another owned page capture')
        client.command('Marionette:SetContext', {'value': 'content'})
        client.command('WebDriver:Navigate', {'url': expected_url})
        time.sleep(2)
        url = client.command('WebDriver:GetCurrentURL')
        if not url.startswith('https://'):
            continue
        prefix = 'page-down-%02d' % len(rows)
        row = dict(requested_url=url)
        rows.append(row)
        output.write_text(json.dumps(result, indent=2)+'\n')
        try:
            time.sleep(2)
            row['admission'] = admit(client, directory / (prefix+'-admission.json'), fixture=False)
            row['before_renderer'] = capture(directory / (prefix+'-before.ppm'))
            client.command('Marionette:SetContext', {'value': 'chrome'})
            script('const w=Services.wm.getMostRecentWindow("navigator:browser");w.gBrowser.selectedBrowser.focus();return true;')
            client.command('Marionette:SetContext', {'value': 'content'})
            row['before'] = script('''window.__sffxPageDownKeys=[];
              window.addEventListener('keydown', e=>{if(window.__sffxPageDownKeys.length<20)
                window.__sffxPageDownKeys.push({trusted:e.isTrusted,key:e.key,code:e.code,keyCode:e.keyCode});},true);
              return {url:location.href,title:document.title,scrollY,scrollHeight:document.documentElement.scrollHeight,
                width:innerWidth,height:innerHeight,devicePixelRatio,text:document.body?.innerText.slice(0,600)};''')
            row['input_status'] = _request(inject, '\n'.join(['key 4e down','key 4e up','wait 250']*5))
            time.sleep(1.8)
            row['after_renderer'] = capture(directory / (prefix+'-after.ppm'))
            row['after'] = script('''return {url:location.href,title:document.title,scrollY,
              keys:window.__sffxPageDownKeys||[],text:document.body?.innerText.slice(0,600),
              readyState:document.readyState,fontsStatus:document.fonts.status};''')
            row['dom_observed_five_trusted_page_downs'] = len(row['after']['keys'])==5 and all(k['trusted'] for k in row['after']['keys'])
            row['five_native_page_downs_delivered'] = 'steps=17 errors=0' in row['input_status']
            row['scrolled'] = row['after']['scrollY'] > row['before']['scrollY']
            row['capture_complete'] = row['five_native_page_downs_delivered'] and row['before']['url']==row['after']['url']
            row['visual_review_pending'] = True
        except Exception as error:
            row['error'] = repr(error)
            row['capture_complete'] = False
        finally:
            output.write_text(json.dumps(result, indent=2)+'\n')
    result['complete'] = len(rows)==4 and all(r.get('capture_complete') for r in rows)
    output.write_text(json.dumps(result, indent=2)+'\n')
    return result
