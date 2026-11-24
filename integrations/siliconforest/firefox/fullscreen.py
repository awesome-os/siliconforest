# SPDX-License-Identifier: MIT
"""Trusted WebDriver click -> DOM video fullscreen -> native frame evidence."""
import json
import os
from pathlib import Path
import re
import secrets
import time


def measure(client, output, deadline):
    client.command('Marionette:SetContext', {'value': 'content'})

    def script(source, args=None):
        return client.command('WebDriver:ExecuteScript', dict(script=source, args=args or [], newSandbox=False))

    for handle in client.command('WebDriver:GetWindowHandles'):
        client.command('WebDriver:SwitchToWindow', {'handle': handle})
        if script('return !!document.querySelector("#fullscreen")&&!!document.querySelector("video");'):
            break
    else:
        raise RuntimeError('Owned video fullscreen fixture not found')
    marker = 'SF-FX fullscreen-' + secrets.token_hex(6)
    script('window.fxTitleOverride=arguments[0];document.title=arguments[0];return true;', [marker])
    element = client.command('WebDriver:FindElement', dict(using='css selector', value='#fullscreen'))
    element_id = element['element-6066-11e4-a52e-4f735466cecf']
    client.command('WebDriver:ElementClick', {'id': element_id})
    until = min(deadline - 15, time.monotonic() + 15)
    state = {}
    while time.monotonic() < until:
        state = script('''const v=document.querySelector("video"),q=v.getVideoPlaybackQuality();
          return {request:window.fxFullscreenRequest,fullscreen:document.fullscreenElement===v,
            paused:v.paused,currentTime:v.currentTime,readyState:v.readyState,error:v.error?.code||null,
            width:v.videoWidth,height:v.videoHeight,total:q.totalVideoFrames,dropped:q.droppedVideoFrames};''')
        if state.get('fullscreen') and state.get('currentTime', 0) >= 3:
            break
        if state.get('error') or state.get('request', {}).get('error'):
            break
        time.sleep(.2)
    result = dict(trusted_webdriver_click=True, physical_input=False, state=state)
    output.write_text(json.dumps(result, indent=2) + '\n')
    from renderer_capture import capture
    result['capture'] = capture(output.with_name('video-fullscreen-renderer.ppm'))
    inject = Path('/private/var/run/gpu-desktop/inject')
    name = 'sffx-fullscreen-' + secrets.token_hex(6)
    if (inject / 'request').exists() or (inject / 'request.part').exists():
        raise RuntimeError('Other inject request pending')
    (inject / 'done').unlink(missing_ok=True)
    with (inject / 'request.part').open('x') as stream:
        stream.write('dump ' + name + '\n')
    os.replace(inject / 'request.part', inject / 'request')
    until = time.monotonic() + 6
    while not (inject / 'done').exists() and time.monotonic() < until:
        time.sleep(.05)
    status = (inject / 'done').read_text()
    if not re.search(r'errors=0\b', status):
        raise RuntimeError(status)
    dump = (inject / (name + '.txt')).read_text()
    output.with_name('video-fullscreen-windows.txt').write_text(dump)
    rows = [line for line in dump.splitlines() if marker in line and 'mapped=true' in line]
    result['native_windows'] = rows
    pattern = r'content=Rect \{ pos: Point \{ x: 0, y: 0 \}, size: Size \{ w: 2560, h: 1600 \}'
    result['native_panel_frame'] = len(rows) == 1 and bool(re.search(pattern, rows[0]))
    client.command('WebDriver:ExecuteAsyncScript', dict(script='''const done=arguments[arguments.length-1];
      document.querySelector("video").pause();
      if(document.fullscreenElement)document.exitFullscreen().then(()=>done(true),e=>done(String(e)));
      else done(false);''', args=[], newSandbox=False))
    result['exited'] = script('return document.fullscreenElement===null;')
    result['complete'] = bool(state.get('request', {}).get('trusted') and state.get('fullscreen') and
        not state.get('error') and not state.get('paused') and state.get('currentTime', 0) >= 3 and
        result['native_panel_frame'] and result['exited'])
    output.write_text(json.dumps(result, indent=2) + '\n')
    return result
