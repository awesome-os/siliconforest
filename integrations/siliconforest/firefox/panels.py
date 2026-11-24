# SPDX-License-Identifier: MIT
"""Owned native open/save picker controls, with admitted synthetic seat input.

Uses Firefox's nsIFilePicker and the public init/open callback contract:
https://raw.githubusercontent.com/mozilla-firefox/firefox/main/widget/nsIFilePicker.idl
The caller holds gui.lock. No existing file or recent-documents list is changed.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import time


def measure(client, output, deadline):
    root = output.parent / 'panels'
    root.mkdir(exist_ok=False)
    payload = b'SiliconForest owned native file picker\n'
    (root / 'open-me.txt').write_bytes(payload)
    result = dict(synthetic_input=True, tests=[], recent_documents=False)
    inject = Path('/private/var/run/gpu-desktop/inject')

    def save():
        output.write_text(json.dumps(result, indent=2) + '\n')

    def chrome(source, args=None):
        client.command('Marionette:SetContext', {'value': 'chrome'})
        return client.command('WebDriver:ExecuteScript', dict(script=source, args=args or [], newSandbox=False))

    def request(source):
        if time.monotonic() > deadline - 10:
            raise RuntimeError('Picker deadline')
        from presentation_seat import _request
        return _request(inject, source)

    for mode, filename in [(0, 'open-me.txt'), (1, 'save-me.txt')]:
        title = 'SF-FX picker-' + secrets.token_hex(6)
        row = dict(mode='open' if mode == 0 else 'save', title=title)
        result['tests'].append(row)
        save()
        row['created'] = chrome('''
          const [title,mode,directory,name]=arguments;
          const w=Services.wm.getMostRecentWindow("navigator:browser");
          const picker=Components.classes["@mozilla.org/filepicker;1"].createInstance(Components.interfaces.nsIFilePicker);
          picker.init(w.browsingContext,title,mode);picker.addToRecentDocs=false;
          const folder=Components.classes["@mozilla.org/file/local;1"].createInstance(Components.interfaces.nsIFile);
          folder.initWithPath(directory);picker.displayDirectory=folder;picker.defaultString=name;
          window.__sffxPicker={picker,result:null};
          setTimeout(()=>{try{picker.open({done(code){let path=null;try{path=picker.file?.path;}catch(error){}
            window.__sffxPicker.result={code,path};}});}catch(error){window.__sffxPicker.result={error:String(error)};}},0);
          return true;''', [title, mode, str(root), filename])
        admitted = []
        until = min(deadline - 15, time.monotonic() + 5)
        name = 'sffx-picker-' + secrets.token_hex(6)
        while time.monotonic() < until:
            request('dump ' + name)
            dump = (inject / (name + '.txt')).read_text()
            admitted = [line for line in dump.splitlines() if title in line and 'mapped=true' in line]
            if len(admitted) == 1:
                break
            time.sleep(.2)
        output.with_name('panel-%d-windows.txt' % mode).write_text(dump)
        row['admitted_windows'] = admitted
        save()
        if len(admitted) != 1:
            row.update(passed=False, error='Unique owned native picker title not found; refused seat input')
            break
        frame = re.search(r'frame=Some\((.*?)\)', admitted[0])
        values = re.findall(r'-?\d+', frame[1]) if frame else []
        if len(values) != 4:
            row.update(passed=False, error='No admitted frame')
            break
        x, y, width, height = map(int, values)
        from presentation_seat import target_window
        target_window(int(re.match(r'window (\d+)',admitted[0])[1]))
        tx, ty = x + width // 2, y + 10
        if not (0 <= tx < 2560 and 0 <= ty < 1600):
            row.update(passed=False, error='Picker title is off screen')
            break
        # SFFilePanel's open list starts unselected. This owned directory has
        # only ../ and open-me.txt; select the file through real list key input.
        select_file = 'key 51 down\nkey 51 up\nkey 51 down\nkey 51 up\n' if mode == 0 else ''
        request(('abs %d %d\nbuttons 1\nwait 50\nbuttons 0\nwait 100\n' %
                (round(tx * 65535 / 2559), round(ty * 65535 / 1599))) +
                select_file + 'key 28 down\nkey 28 up\nwait 400')
        row['input'] = 'owned list Down/Down/Enter' if mode == 0 else 'owned name field Enter'
        # A nested native modal loop may delay Gecko automation. Inspect the
        # compositor before querying Gecko, and cancel only the admitted picker.
        request('dump ' + name)
        still = (inject / (name + '.txt')).read_text()
        if sum(title in line and 'mapped=true' in line for line in still.splitlines()) == 1:
            request('key 29 down\nkey 29 up\nwait 250')
            row['accepted'] = False
        actual = chrome('return window.__sffxPicker.result;')
        row['result'] = actual
        expected = root / filename
        row['passed'] = bool(actual and actual.get('code') in [0, 2] and actual.get('path') == str(expected))
        if row['passed'] and mode == 1:
            chrome('IOUtils.writeUTF8(arguments[0],arguments[1]);return true;', [str(expected), payload.decode()])
            until = min(deadline - 5, time.monotonic() + 3)
            while not expected.exists() and time.monotonic() < until:
                time.sleep(.1)
        if expected.exists():
            got = expected.read_bytes()
            row.update(bytes=len(got), sha256=hashlib.sha256(got).hexdigest())
            row['passed'] &= got == payload
        elif mode == 1:
            row['passed'] = False
        save()
    result['complete'] = len(result['tests']) == 2 and all(row.get('passed') for row in result['tests'])
    save()
    return result
