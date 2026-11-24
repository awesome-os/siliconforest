# SPDX-License-Identifier: MIT
"""Repeat the installed Firefox hamburger regression through the virtual seat.

Caller owns the browser session and gui.lock. Every click targets the uniquely
admitted browser window; actual trusted button events and native window survival
are checked separately from Gecko's popup state. This is functional QA only.
"""
import json
from pathlib import Path
import re
import secrets
import time


def menu_button_in_renderer(path, frame):
    """Locate the three purple menu strokes in the admitted light chrome.

    This deliberately refuses an absent/ambiguous icon. Firefox's reported
    screen origin is incorrect on this platform; trusted button events below
    independently confirm that the physical click reached the intended control.
    """
    data = path.read_bytes()
    header = re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s', data)
    if not header:
        raise ValueError('Expected actual renderer P6 capture')
    width, height = map(int, header.groups())
    pixels = memoryview(data)[header.end():]
    if len(pixels) != width*height*3:
        raise ValueError('Invalid renderer pixel count')
    fx, fy, fw, fh = frame
    rows = {}
    for y in range(max(0, fy+45), min(height, fy+250, fy+fh)):
        start = None
        for x in range(max(0, fx+fw-100), min(width, fx+fw-12)):
            r, g, b = pixels[(y*width+x)*3:(y*width+x)*3+3]
            ink = b >= r+6 and b >= g+6 and r < 230 and g < 230
            if ink and start is None:
                start = x
            if not ink and start is not None:
                if 8 <= x-start <= 32:
                    rows.setdefault((start, x-start), []).append(y)
                start = None
    clusters = []
    for (x, length), ys in rows.items():
        for y in ys:
            for spacing in range(3, 13):
                if y+spacing in ys and y+2*spacing in ys:
                    point = (x+(length-1)/2, y+spacing)
                    for cluster in clusters:
                        if abs(point[0]-cluster[0][0]) < 2 and abs(point[1]-cluster[0][1]) <= 3:
                            cluster.append(point)
                            break
                    else:
                        clusters.append([point])
    candidates = [(sum(p[0] for p in c)/len(c), sum(p[1] for p in c)/len(c)) for c in clusters]
    if len(candidates) != 1:
        raise RuntimeError('Expected one unambiguous three-stroke menu icon in the admitted renderer frame')
    return dict(x=candidates[0][0], y=candidates[0][1], method='actual-renderer-three-stroke-light-chrome-icon')


def measure(client, output, deadline, admission=None, observe_auto_close=False):
    from presentation_seat import admit, _request
    from renderer_capture import capture
    inject = Path('/private/var/run/gpu-desktop/inject')
    result = dict(complete=False, synthetic_input=True, virtual_seat=True,
                  requested_repetitions=20, repetitions=[], timing_qualified=False,
                  protocol_diagnostic=observe_auto_close)

    def save():
        output.write_text(json.dumps(result, indent=2) + '\n')

    def chrome(source):
        client.command('Marionette:SetContext', {'value': 'chrome'})
        return client.command('WebDriver:ExecuteScript', dict(script=source, args=[], newSandbox=False))

    def request(lines):
        if time.monotonic() > deadline - 10:
            raise RuntimeError('Popup stress deadline')
        return _request(inject, lines)

    def state():
        current = chrome('''const w=Services.wm.getMostRecentWindow("navigator:browser");
          if(!w)return {windows:0};
          return {windows:[...Services.wm.getEnumerator("navigator:browser")].length,
            panel:w.document.getElementById("appMenu-popup")?.state,
            events:w.__sffxPopupEvents??[],input:w.__sffxPopupInput??[],
            windowInput:w.__sffxPopupWindowInput??[],
            focusEvents:w.__sffxPopupFocus??[],documentFocused:w.document.hasFocus(),
            lifecycle:w.__sffxPopupLifecycle??[],tabs:w.gBrowser.tabs.length};''')
        unexpected = [event for event in current.get('windowInput', [])
                      if event['type'] == 'mousedown' and event.get('target') != 'PanelUI-menu-button']
        if unexpected:
            result['unexpected_window_input'] = unexpected
            result['state_at_unexpected_input'] = current
            save()
            raise RuntimeError('Unrequested mouse input reached the owned window; stop further synthetic input')
        return current

    try:
        if admission is None:
            admission = admit(client, output.with_name('popup-admission.json'), fixture=False)
        result['admission'] = admission
        name = 'sffx-popup-frame-' + secrets.token_hex(5)
        request('dump ' + name)
        dump = (inject / (name + '.txt')).read_text()
        output.with_name('popup-current-frame.txt').write_text(dump)
        native = [line for line in dump.splitlines()
                  if re.match(r'window %d ' % admission['window_id'], line) and 'mapped=true' in line]
        frame = re.search(r'frame=Some\((.*?)\)', native[0]) if len(native) == 1 else None
        values = list(map(int, re.findall(r'-?\d+', frame[1]))) if frame else []
        if len(values) != 4:
            raise RuntimeError('Admitted owned popup-test window is no longer mapped')
        result['current_frame'] = values
        result['before'] = state()
        if result['before']['windows'] != 1:
            raise RuntimeError('Expected one owned browser window')
        chrome('''const w=Services.wm.getMostRecentWindow("navigator:browser");
          const b=w.document.getElementById("PanelUI-menu-button");
          if(!b)throw new Error("No application menu button");
          w.__sffxPopupEvents=[];
          w.__sffxPopupInput=[];w.__sffxPopupLifecycle=[];w.__sffxPopupWindowInput=[];w.__sffxPopupFocus=[];
          for(const type of ["focus","blur","activate","deactivate"])
            w.addEventListener(type,e=>w.__sffxPopupFocus.push({type,time:performance.now(),
              target:e.target===w?"window":e.target.id,documentFocused:w.document.hasFocus()}),true);
          b.addEventListener("mousedown",e=>w.__sffxPopupEvents.push({trusted:e.isTrusted,
            button:e.button,ctrl:e.ctrlKey,alt:e.altKey,meta:e.metaKey,shift:e.shiftKey}),true);
          for(const type of ["mousedown","mouseup","click"])
            b.addEventListener(type,e=>w.__sffxPopupInput.push({type,trusted:e.isTrusted,
              button:e.button,time:performance.now(),target:e.target.id}),true);
          for(const type of ["mousedown","mouseup","click"])
            w.addEventListener(type,e=>w.__sffxPopupWindowInput.push({type,trusted:e.isTrusted,
              button:e.button,time:performance.now(),target:e.target.id,
              client:[e.clientX,e.clientY],screen:[e.screenX,e.screenY]}),true);
          const panel=w.document.getElementById("appMenu-popup");
          for(const type of ["popupshowing","popupshown","popuphiding","popuphidden"])
            panel.addEventListener(type,e=>w.__sffxPopupLifecycle.push({type,time:performance.now(),
              documentFocused:w.document.hasFocus(),stack:type==="popuphiding"?new Error().stack:null}),true);
          return true;''')
        calibration_path = output.with_name('popup-chrome-calibration.ppm')
        result['chrome_capture'] = capture(calibration_path)
        result['button_pixel_location'] = point = menu_button_in_renderer(calibration_path, values)
        save()
        for index in range(20):
            x, y, width, height = result['current_frame']
            px, py = point['x'], point['y']
            if not (0 <= px <= 2559 and 0 <= py <= 1599 and x <= px < x+width and y <= py < y+height):
                raise RuntimeError('Application menu button lies outside the admitted owned frame')
            row = dict(attempt=index+1, point=point, complete=False)
            result['repetitions'].append(row)
            save()
            row['click_inject_status'] = request('abs %d %d\nbuttons 1\nwait 50\nbuttons 0\nwait 250' %
                    (round(px*65535/2559), round(py*65535/1599)))
            row['opened'] = state()
            # First-open module loading can outlast the injected click. Keep
            # actual event history and a bounded state wait; never infer that
            # a mousedown alone opened a native popup.
            opening_end = min(deadline-10, time.monotonic()+3)
            while row['opened'].get('panel') != 'open' and time.monotonic() < opening_end:
                if observe_auto_close:
                    presses = [e for e in row['opened'].get('input', []) if e['type'] == 'mousedown']
                    if presses and any(e['type'] == 'popuphidden' and e['time'] >= presses[-1]['time']
                                       for e in row['opened'].get('lifecycle', [])):
                        break
                time.sleep(.1)
                row['opened'] = state()
            save()
            events = row['opened'].get('events', [])
            row['menu_stayed_open'] = row['opened'].get('panel') == 'open'
            if (len(events) != index+1 or
                    not all(event['trusted'] and event['button'] == 0 for event in events)):
                raise RuntimeError('Application menu did not receive the trusted virtual-seat click')
            if not row['menu_stayed_open'] and not observe_auto_close:
                raise RuntimeError('Application menu did not stay open after the trusted virtual-seat click')
            if index in [0, 4, 9, 14, 19] and row['menu_stayed_open']:
                row['capture'] = capture(output.with_name('popup-%02d-renderer.ppm' % (index+1)))
            elif index in [0, 4, 9, 14, 19]:
                row['capture_omitted'] = 'Protocol-only diagnostic: menu already closed; no menu pixel proof'
            request('key 29 down\nkey 29 up\nwait 150')
            row['closed'] = state()
            name = 'sffx-popup-' + secrets.token_hex(5)
            request('dump ' + name)
            dump = (inject / (name + '.txt')).read_text()
            output.with_name('popup-%02d-windows.txt' % (index+1)).write_text(dump)
            mapped = [line for line in dump.splitlines()
                      if re.match(r'window %d ' % admission['window_id'], line) and 'mapped=true' in line]
            row['window_survived'] = bool(len(mapped) == 1 and row['closed'].get('windows') == 1 and
                                          row['closed'].get('panel') == 'closed')
            row['complete'] = row['menu_stayed_open'] and row['window_survived']
            save()
            if not row['window_survived']:
                raise RuntimeError('Application menu close lost the browser window or left a popup open')
        result['after'] = state()
        result['complete'] = len(result['repetitions']) == 20 and all(row['complete'] for row in result['repetitions'])
        result['protocol_cycles_survived'] = len(result['repetitions']) == 20 and all(
            row['window_survived'] for row in result['repetitions'])
    finally:
        save()
    return result
