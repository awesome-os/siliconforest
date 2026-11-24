# SPDX-License-Identifier: MIT
"""Focus an admitted owned fixture and prove a live renderer before timing."""
import json
import os
from pathlib import Path
import re
import secrets
import time
from contextvars import ContextVar

_allow_owner_activity=ContextVar('sffx_functional_qa',default=False)
_target_window=ContextVar('sffx_owned_virtual_target',default=None)


def functional_qa(enabled=True):
    """Owner-authorized functional checks record activity; timings stay unqualified."""
    _allow_owner_activity.set(bool(enabled))


def functional_qa_enabled():
    return _allow_owner_activity.get()


def target_window(window_id):
    """Set only after this caller admits its own unique window in the dump."""
    if type(window_id) is not int or window_id<=0:
        raise ValueError('Expected an admitted window ID')
    _target_window.set(window_id)


def owner_idle(inject, require_virtual=False):
    marker=inject.parent/'owner-input'
    if not marker.exists():
        if require_virtual:raise RuntimeError('Virtual seat not installed: refuse pointer/key injection')
        return
    text=marker.read_text()
    active=re.search(r'^active=([01])$',text,re.MULTILINE)
    if 'DESK7' not in text or active is None or (active[1]!='0' and not _allow_owner_activity.get()):
        raise RuntimeError('Owner input active or unrecognized: stop synthetic input/capture')


def _request(inject, lines, _virtual_handshake=False, timeout=6):
    if not 0 < timeout <= 30:
        raise ValueError('Virtual-seat request timeout must be bounded by 30 seconds')
    commands=[line.split()[0] for line in lines.splitlines() if line.split()]
    mutating=any(command not in ['dump','list','stats','capture','wait'] for command in commands)
    if any(command=='capture' for command in commands):owner_idle(inject)
    if mutating and not _virtual_handshake:
        owner_idle(inject,require_virtual=True)
        status=_request(inject,'seat virtual\nstats',_virtual_handshake=True)
        if not _allow_owner_activity.get() and (not re.search(r'owner_seen_active=0\b',status) or not re.search(r'owner_active=0\b',status)):
            raise RuntimeError('Compositor did not confirm idle-owner virtual-seat protocol')
        owner_idle(inject,require_virtual=True)
        if any(line.strip()=='seat owner' for line in lines.splitlines()):raise RuntimeError('Owner seat forbidden in Firefox diagnostics')
        target=_target_window.get()
        # Reopen/Quit address an admitted process and must work with no window.
        # Pointer/key streams always retain an explicit window target.
        process_request=all(command in ['app','wait','dump','list','stats','capture'] for command in commands)
        if target is None and not process_request:
            raise RuntimeError('Admit an owned window before synthetic input')
        lines='seat virtual\n'+('target %d\n'%target if not process_request else '')+lines
    if (inject / 'request').exists() or (inject / 'request.part').exists():
        raise RuntimeError('Another inject request is pending')
    (inject / 'done').unlink(missing_ok=True)
    with (inject / 'request.part').open('x') as stream:
        stream.write(lines + '\n')
    os.replace(inject / 'request.part', inject / 'request')
    end = time.monotonic()+timeout
    while not (inject / 'done').exists() and time.monotonic() < end:
        time.sleep(.05)
    status = (inject / 'done').read_text()
    if not re.search(r'errors=0\b', status):
        raise RuntimeError(status)
    if mutating and not _allow_owner_activity.get() and (re.search(r'owner_seen_active=1\b',status) or re.search(r'owner_active=1\b',status)):
        raise RuntimeError('Real owner input interrupted this synthetic request; discard and stop')
    return status


def admit(client, output, fixture=True, verify_renderer=True, handle=None, seat_input=True):
    def script(source, args=None):
        return client.command('WebDriver:ExecuteScript', dict(script=source, args=args or [], newSandbox=False))

    client.command('Marionette:SetContext', {'value': 'content'})
    available = client.command('WebDriver:GetWindowHandles')
    if handle is not None and handle not in available:
        raise RuntimeError('Requested owned admission tab no longer exists')
    for candidate in ([handle] if handle is not None else available):
        client.command('WebDriver:SwitchToWindow', {'handle': candidate})
        if not fixture or script('return typeof fxMeasureFrames === "function";'):
            break
    else:
        raise RuntimeError('Owned presentation fixture absent')
    marker = 'SF-FX presentation-' + secrets.token_hex(8)
    previous = script('''return {title:document.title,video:typeof fxVideoResult==="object",
      override:window.fxTitleOverride??null};''')
    if fixture:
        client.command('WebDriver:ExecuteAsyncScript', dict(script='''
          const marker=arguments[0],done=arguments[arguments.length-1];
          fxMeasureFrames(250).then(()=>{document.title=marker;done(true);});''', args=[marker], newSandbox=False))
    else:
        script('if(arguments[1])window.fxTitleOverride=arguments[0];document.title=arguments[0];return true;',
               [marker, previous['video']])
        time.sleep(.25)
    inject = Path('/private/var/run/gpu-desktop/inject')

    def request(lines):
        _request(inject, lines)

    name = 'sffx-presentation-' + secrets.token_hex(5)
    request('dump ' + name)
    dump = (inject / (name + '.txt')).read_text()
    output.with_suffix('.windows.txt').write_text(dump)
    matches = [line for line in dump.splitlines() if marker in line and 'mapped=true' in line and 'org.mozilla.firefox' in line]
    if len(matches) != 1:
        raise RuntimeError('Expected exactly one uniquely titled owned fixture frame')
    if seat_input:
        target_window(int(re.match(r'window (\d+)',matches[0])[1]))
    frame = re.search(r'frame=Some\((.*?)\)', matches[0])
    values = tuple(map(int, re.findall(r'-?\d+', frame[1]))) if frame else ()
    if len(values) != 4:
        raise RuntimeError('Owned frame geometry absent')
    x, y, width, height = values
    tx, ty = x+width//2, y+10
    if not (width>100 and height>100 and 0<=tx<=2559 and 0<=ty<=1599):
        raise RuntimeError('Owned titlebar outside the admitted panel')
    # Wake with a modifier tap on this explicit virtual target. Recent desktop
    # builds do not wake the panel on virtual pointer motion alone. No desktop
    # power/idle configuration is changed.
    if seat_input:
        request('key e1 down\nkey e1 up\nabs %d %d\nwait 500\nbuttons 1\nwait 60\nbuttons 0\nwait 300' %
                (round(tx*65535/2559), round(ty*65535/1599)))
    client.command('Marionette:SetContext', {'value': 'chrome'})
    focused = script('const w=Services.wm.getMostRecentWindow("navigator:browser");return !!w&&w.document.hasFocus();')
    if seat_input and not focused and not _allow_owner_activity.get():
        raise RuntimeError('Admitted Firefox did not acquire focus')
    from renderer_capture import capture
    renderer = capture(output.with_suffix('.ppm')) if verify_renderer else None
    client.command('Marionette:SetContext', {'value': 'content'})
    script('''document.title=arguments[0];if(arguments[1]){
      if(arguments[2]===null)delete window.fxTitleOverride;else window.fxTitleOverride=arguments[2];}return true;''',
           [previous['title'], previous['video'], previous['override']])
    result = dict(complete=True, synthetic_seat_input=seat_input, focus_qualified=bool(seat_input and focused), admitted_unique_title=marker,
                  requested_handle=handle, admitted_handle=candidate,
                  frame=values, window_id=int(re.match(r"window (\d+) ", matches[0])[1]),
                  focused=focused, renderer=renderer)
    output.write_text(json.dumps(result, indent=2)+'\n')
    return result


def pulse(admission, output):
    """Wake via pointer motion only after fresh own-process/frame/stack checks."""
    import subprocess
    admission = Path(admission)
    if not str(admission).startswith('/private/var/db/sffx/') or '..' in admission.parts:
        raise ValueError('Admission must be in the owned private prefix')
    data = json.loads(admission.read_text())
    providers = json.loads(admission.with_name('providers.json').read_text())
    parents = [row['pid'] for row in providers['processes'] if row['role'] == 'parent']
    if len(parents) != 1:
        raise RuntimeError('Owned benchmark parent absent')
    command = subprocess.run(['ps','-p',str(parents[0]),'-o','command='],capture_output=True,text=True).stdout
    if str(admission.parent/'profile') not in command:
        raise RuntimeError('Owned benchmark parent identity no longer matches its profile')
    window = data.get('window_id')
    if window is None:
        old = admission.with_suffix('.windows.txt').read_text()
        matches = [line for line in old.splitlines() if data['admitted_unique_title'] in line and 'org.mozilla.firefox' in line]
        if len(matches) != 1:
            raise RuntimeError('Original unique window admission absent')
        window = int(re.match(r'window (\d+) ', matches[0])[1])
    inject = Path('/private/var/run/gpu-desktop/inject')
    name = 'sffx-bench-pulse-'+secrets.token_hex(5)
    _request(inject, 'dump '+name)
    dump = (inject/(name+'.txt')).read_text()
    lines = [line for line in dump.splitlines() if line.startswith('window %d ' % window)
             and 'org.mozilla.firefox' in line and 'mapped=true' in line]
    stack = re.search(r'^stack \[(.*?)\]$',dump,re.MULTILINE)
    top = [int(value) for value in re.findall(r'\d+',stack[1])] if stack else []
    if len(lines) != 1 or not top or top[-1] != window:
        raise RuntimeError('Owned benchmark window is no longer the top mapped window')
    frame = re.search(r'frame=Some\((.*?)\)',lines[0])
    values = tuple(map(int,re.findall(r'-?\d+',frame[1]))) if frame else ()
    if len(values) != 4:
        raise RuntimeError('Owned benchmark frame absent')
    x,y,width,height = values;px=x+width//2;py=max(2,y+10)
    if not (width>100 and height>100 and x<=px-2<px<x+width and y<=py<y+height
            and 2<=px<=2559 and 0<=py<=1599):
        raise RuntimeError('Owned benchmark title region outside panel')
    def point(a):return round(a*65535/2559)
    _request(inject,'abs %d %d\nwait 60\nabs %d %d' %
             (point(px-2),round(py*65535/1599),point(px),round(py*65535/1599)))
    result = dict(complete=True,synthetic_pointer_only=True,parent=parents[0],window=window,frame=values)
    Path(output).write_text(json.dumps(result,indent=2)+'\n')
    return result
