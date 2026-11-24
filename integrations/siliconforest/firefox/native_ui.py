# SPDX-License-Identifier: MIT
"""Finite native seat/menu controls, confined to the owned Firefox process.

Caller holds the shared gui.lock. Dump -> PID/frame admission precedes focus;
all key events then traverse the compositor seat and AppKit. No clipboard writes.
"""
import json
import os
from pathlib import Path
import re
import socket
import secrets
import time


def measure(client, output, deadline, clipboard_guard=None, native_drag=False, clipboard_empty=False, precise_scroll=False):
    result = {'synthetic_input': True, 'tests': []}
    inject = Path('/private/var/run/gpu-desktop/inject')
    if not inject.is_dir():
        raise RuntimeError('Native input requires the existing inject-capable compositor')

    def save():
        output.write_text(json.dumps(result, indent=2) + '\n')

    def chrome(script):
        client.command('Marionette:SetContext', {'value': 'chrome'})
        return client.command('WebDriver:ExecuteScript', {
            'script': script, 'args': [], 'newSandbox': False})

    client.command('Marionette:SetContext', {'value': 'content'})
    fixture_handle = None
    original_handles = client.command('WebDriver:GetWindowHandles')
    for handle in original_handles:
        client.command('WebDriver:SwitchToWindow', {'handle': handle})
        found = client.command('WebDriver:ExecuteScript', {'script':
            'return typeof window.fxMeasureFrames === "function";', 'args': [], 'newSandbox': False})
        if found:
            fixture_handle = handle
            break
    if fixture_handle is None:
        raise RuntimeError('Owned native UI fixture not found')
    pid = int(chrome('return Components.classes["@mozilla.org/xre/app-info;1"]'
                     '.getService(Components.interfaces.nsIXULRuntime).processID;'))
    marker = 'SF-FX UI %d-%s' % (pid, secrets.token_hex(4))
    client.command('Marionette:SetContext', {'value': 'content'})
    client.command('WebDriver:ExecuteAsyncScript', {'script':
        'const title=arguments[0],done=arguments[arguments.length-1];'
        'fxMeasureFrames(250).then(()=>{document.title=title;done(document.title);});',
        'args': [marker], 'newSandbox': False, 'scriptTimeout': 3000})
    chrome('const w=Services.wm.getMostRecentWindow("navigator:browser"); w.focus(); return true;')
    time.sleep(.3)
    result['browser_pid'] = pid
    result['title_marker'] = marker
    result['admission'] = 'unique native title set through this owned-profile session; compositor dump has no PID field'
    save()
    dumpname = 'sffx-ui-' + str(pid)

    def request(lines):
        if time.monotonic() > deadline - 8:
            raise RuntimeError('Native UI deadline')
        from presentation_seat import _request
        return _request(inject, lines)

    request('dump ' + dumpname)
    dump = (inject / (dumpname + '.txt')).read_text()
    output.with_suffix('.windows.txt').write_text(dump)
    admitted = []
    for line in dump.splitlines():
        if 'mapped=true' not in line or 'org.mozilla.firefox' not in line:
            continue
        if marker not in line:
            continue
        frame = re.search(r'frame=Some\((.*?)\)', line)
        if frame:
            values = re.findall(r'-?\d+', frame[1])
            if len(values) == 4:
                admitted.append(tuple(map(int, values)))
    if len(admitted) != 1:
        result['error'] = 'Expected exactly one mapped Firefox frame with the owned unique native title'
        save()
        return result
    x, y, width, height = admitted[0]
    if not (width > 100 and height > 100 and 0 <= x < 2500 and 0 <= y < 1550):
        raise RuntimeError('Owned frame geometry out of bounds')
    result['admitted_frame'] = admitted[0]
    owned_line = next(line for line in dump.splitlines() if marker in line and 'mapped=true' in line)
    window_id = int(re.match(r'window (\d+) ', owned_line)[1])
    result['admitted_window_id'] = window_id
    from presentation_seat import target_window
    target_window(window_id)
    from renderer_capture import capture as capture_renderer
    tx, ty = x + width // 2, y + 10
    if tx > 2559 or ty > 1599:
        raise RuntimeError('Owned titlebar outside panel')
    request('abs %d %d\nwait 60\nbuttons 1\nwait 60\nbuttons 0\nwait 250' %
            (round(tx * 65535 / 2559), round(ty * 65535 / 1599)))
    result['admission_capture'] = capture_renderer(output.with_name('native-admission-renderer.ppm'))
    save()

    def state():
        return chrome('''const w=Services.wm.getMostRecentWindow("navigator:browser");
          return w?{focused:w.document.hasFocus(),tabs:w.gBrowser.tabs.length,
            active:w.document.activeElement?.id,uri:w.gBrowser.selectedBrowser.currentURI.spec,
            zoom:w.gBrowser.selectedBrowser.fullZoom}:{focused:false,tabs:0,noBrowserWindow:true};''')

    def check(name, actual, passed):
        result['tests'].append(dict(name=name, actual=actual, passed=bool(passed)))
        save()

    menu_path = Path('/private/var/run/gpu-desktop/app-menu') / str(pid)
    def menu(command):
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.settimeout(2)
            connection.connect(str(menu_path))
            connection.sendall((command + '\n').encode())
            connection.shutdown(socket.SHUT_WR)
            data = bytearray()
            while len(data) < 1024 * 1024:
                block = connection.recv(8192)
                if not block:
                    return data.decode('utf-8')
                data.extend(block)
            raise RuntimeError('Oversized owned menu reply')
    try:
        result['menubar'] = menu('menubar')
        rows = menu('menu 1')
        result['native_menu'] = rows
        check('native-menu-socket', rows, rows.startswith('item ') and '\nend\n' in rows)
    except (OSError, RuntimeError) as error:
        check('native-menu-socket', str(error), False)

    before = state()
    result['initial'] = before
    if not before['focused']:
        result['error'] = 'Owned browser did not acquire focus; refused keyboard input'
        save()
        return result

    def chord(key, shift=False, restore=False):
        if not state()['focused']:
            raise RuntimeError('Owned browser lost focus; refused keyboard input')
        result['last_action'] = dict(key=key, shift=shift)
        save()
        lines = ['key e3 down'] + (['key e1 down'] if shift else [])
        lines += ['key %02x down' % key, 'key %02x up' % key]
        lines += (['key e1 up'] if shift else []) + ['key e3 up', 'wait 400']
        request('\n'.join(lines))
        if restore:
            client.command('Marionette:SetContext', {'value': 'content'})
            result['after_close_content_handles'] = client.command('WebDriver:GetWindowHandles')
            result['original_fixture_handle'] = fixture_handle
            client.command('Marionette:SetContext', {'value': 'chrome'})
            windows = client.command('WebDriver:GetWindowHandles')
            result['after_close_chrome_handles'] = windows
            save()
            if not windows:
                return dict(focused=False, tabs=0, noBrowserWindow=True)
            client.command('WebDriver:SwitchToWindow', {'handle': windows[0]})
        return state()

    actual = chord(0x0f)  # Command-L
    check('Command-L', actual, actual['focused'] and 'urlbar' in (actual['active'] or ''))
    actual = chord(0x17)  # Command-T
    check('Command-T', actual, actual['tabs'] == before['tabs'] + 1)
    client.command('Marionette:SetContext', {'value': 'content'})
    added_handles = [handle for handle in client.command('WebDriver:GetWindowHandles') if handle not in original_handles]
    client.command('WebDriver:SwitchToWindow', {'handle': fixture_handle})
    actual = chord(0x2e, shift=True)  # Command-+ zoom in
    check('Command-zoom-in', actual, actual['zoom'] > before['zoom'])
    actual = chord(0x2d)  # Command--
    check('Command-zoom-out', actual, abs(actual['zoom'] - before['zoom']) < .001)
    chord(0x27)  # Command-0 restores normal zoom in this isolated profile
    actual = chord(0x15)  # Command-R
    client.command('Marionette:SetContext', {'value': 'content'})
    handles = client.command('WebDriver:GetWindowHandles')
    if fixture_handle in handles:
        client.command('WebDriver:SwitchToWindow', {'handle': fixture_handle})
    reload_type = client.command('WebDriver:ExecuteScript', {'script':
        'return performance.getEntriesByType("navigation")[0]?.type;', 'args': [], 'newSandbox': False})
    check('Command-R', dict(state=actual, navigation_type=reload_type),
          actual['uri'] == before['uri'] and reload_type == 'reload')

    client.command('Marionette:SetContext', {'value': 'content'})
    client.command('WebDriver:ExecuteScript', dict(script='''window.__sffxContexts=0;
      document.addEventListener("contextmenu",()=>window.__sffxContexts++);return true;''', args=[], newSandbox=False))
    cx, cy = x + width // 2, y + height // 2
    request('abs %d %d\nbuttons 2\nwait 50\nbuttons 0\nwait 250' %
            (round(cx * 65535 / 2559), round(cy * 65535 / 1599)))
    context_name = dumpname + '-context'
    request('dump ' + context_name)
    context_dump = (inject / (context_name + '.txt')).read_text()
    output.with_name('context-menu-windows.txt').write_text(context_dump)
    popups = [line for line in context_dump.splitlines()
              if line.startswith('shell ') and 'mapped=true' in line and 'owner=Some(%d)' % window_id in line]
    from renderer_capture import capture as capture_renderer
    context_capture = capture_renderer(output.with_name('context-menu-renderer.ppm'))
    stack = re.search(r'^stack \[(.*?)\]$', context_dump, re.MULTILINE)
    top = [int(value) for value in re.findall(r'\d+', stack[1])] if stack else []
    if not popups and (not top or top[-1] != window_id):
        raise RuntimeError('Context menu lost the admitted window; refused Escape')
    request('key 29 down\nkey 29 up\nwait 250')
    client.command('Marionette:SetContext', {'value': 'content'})
    contexts = client.command('WebDriver:ExecuteScript', dict(script='return window.__sffxContexts;', args=[], newSandbox=False))
    check('native-context-menu', dict(content_events=contexts, owned_popups=popups, capture=context_capture),
          contexts == 1 and bool(popups))

    if precise_scroll:
        from precise_scroll import measure as measure_precise_scroll
        actual = measure_precise_scroll(client, request, admitted[0])
        check('native-precise-pixel-scroll', actual, actual.get('complete'))
    if native_drag:
        from drag_drop import measure as measure_drag
        actual = measure_drag(client, request, admitted[0])
        check('native-drag-drop', actual, actual.get('complete'))
    if clipboard_empty:
        from clipboard_empty import measure as measure_clipboard_empty
        actual = measure_clipboard_empty(client, chord)
        check('native-clipboard-originally-empty', actual, actual.get('complete'))
    if clipboard_guard:
        from clipboard_probe import measure as measure_clipboard
        actual = measure_clipboard(client, clipboard_guard, chord)
        check('native-clipboard', actual, actual.get('complete'))

    if len(added_handles) == 1:
        client.command('Marionette:SetContext', {'value': 'content'})
        client.command('WebDriver:SwitchToWindow', {'handle': added_handles[0]})
        actual = chord(0x1a, restore=True)  # Command-W, close only the tab just created
        check('Command-W', actual, actual['tabs'] == before['tabs'])
        if actual.get('noBrowserWindow'):
            result['complete'] = False
            result['error'] = 'Command-W closed the browser window instead of only its selected tab'
            save()
            return result

    result['complete'] = all(test['passed'] for test in result['tests'])
    save()
    return result
