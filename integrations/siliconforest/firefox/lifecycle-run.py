#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Finite owned-profile native-menu/audio lifecycle diagnostic on SiliconForest.

Caller holds gui.lock, checks coordination and cold-verifies these tools first.
Default launches use the actual installed outer launcher. Explicit private UI
comparisons use the runtime plan and are labelled separately from installed QA.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import signal
import shutil
import subprocess
import time

from lifecycle_observer import AudioFixture, close_button, gecko_windows, native_menu, window_button
from marionette import Marionette
from owner_guard import OwnerInputGuard


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def private_ui_environment(env, runtime, prefix):
    """Select an explicit comparison even when the installed cohort is system UI."""
    result = dict(env)
    for key, component in [('DYLD_FRAMEWORK_PATH', 'appkit'),
                           ('DYLD_LIBRARY_PATH', 'windowserver')]:
        previous = str(runtime / component)
        selected = str(prefix / component)
        entries = [p for p in result.get(key, '').split(':')
                   if p and p not in [previous, selected]]
        result[key] = ':'.join([selected] + entries)
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--run', type=Path, required=True)
    ap.add_argument('--action', choices=['close-tab', 'close-window', 'hide', 'minimize', 'quit', 'desktop-quit', 'windowless-quit', 'close-box', 'cmd-w', 'connection-loss'], required=True)
    ap.add_argument('--ui-prefix', type=Path)
    ap.add_argument('--system-coretext', action='store_true')
    ap.add_argument('--observer', type=Path)
    ap.add_argument('--loss-observer', type=Path)
    ap.add_argument('--runtime-plan', action='store_true', help='diagnostic comparison; bypass installed outer launcher')
    ap.add_argument('--ipc-quit-only', action='store_true',
                    help='desktop-quit control: admit the owned window without pointer/key input')
    ap.add_argument('--quit-menu-control', action='store_true',
                    help='windowless-quit diagnostic: invoke the app-owned Quit menu; cannot qualify desktop routing')
    ap.add_argument('--sample-path', default='/private/var/db/sffx/probes-v3/sf-sample')
    args = ap.parse_args()
    if args.ipc_quit_only and args.action != 'desktop-quit':
        raise ValueError('Input-free IPC control is limited to desktop-quit')
    if args.quit_menu_control and args.action != 'windowless-quit':
        raise ValueError('Quit menu control is limited to the windowless diagnostic')
    if os.uname().sysname != 'Darwin':
        raise RuntimeError('Target-only diagnostic')
    for path in [args.run, args.ui_prefix, args.observer, args.loss_observer]:
        if path is not None and (not str(path).startswith('/private/var/db/sffx/') or '..' in path.parts):
            raise ValueError('Only the owned private target prefix is allowed')
    guard = OwnerInputGuard(require_idle=False)
    from presentation_seat import functional_qa
    functional_qa()
    args.run.mkdir(exist_ok=False)
    profile = args.run / 'profile'
    profile.mkdir()
    home = args.run / 'home'
    home.mkdir()
    prefs = {'browser.shell.checkDefaultBrowser': False, 'app.update.auto': False,
             'browser.aboutwelcome.enabled': False, 'browser.preonboarding.enabled': False,
             'browser.startup.homepage_override.mstone': 'ignore',
             'startup.homepage_welcome_url': '', 'startup.homepage_welcome_url.additional': '',
             'datareporting.policy.dataSubmissionEnabled': False, 'toolkit.telemetry.enabled': False,
             'termsofuse.bypassNotification': True,  # isolated test profile, not owner consent
             'webgl.disable-angle': True, 'browser.tabs.inTitlebar': 0,
             'marionette.port': 28290, 'media.autoplay.default': 0,
             'dom.ipc.processPrelaunch.enabled': False, 'dom.ipc.keepProcessesAlive.web': 0}
    (profile / 'user.js').write_text(''.join('user_pref(%s,%s);\n' % (json.dumps(k), json.dumps(v)) for k, v in prefs.items()))
    fixture = AudioFixture(args.run)
    runtime = Path('/usr/local/lib/siliconforest/firefox/160-system1')
    manifest = json.loads((runtime / 'release.json').read_text())
    arguments = ['--no-remote', '--profile', str(profile), '--marionette', fixture.url]
    installed_outer = not (args.runtime_plan or args.ui_prefix or args.system_coretext)
    if args.action == 'connection-loss' and not (installed_outer and args.observer and args.loss_observer):
        raise ValueError('Connection loss requires the actual installed launcher and both qualified observers')
    if args.loss_observer and args.action != 'connection-loss':
        raise ValueError('Loss observer is only allowed in the explicit connection-loss action')
    if installed_outer:
        command = ['/Applications/Firefox.app/Contents/MacOS/firefox-sf'] + arguments
        env = os.environ.copy()
    else:
        command, env = module('daily', 'daily-launch.py').plan(runtime, arguments)
    env.update(MOZ_NO_REMOTE='1', MOZ_REMOTE_ALLOW_SYSTEM_ACCESS='1', MOZ_CRASHREPORTER_DISABLE='1',
               HOME=str(home), CFFIXED_USER_HOME=str(home))
    if args.ui_prefix:
        # Firefox requests Versions/C. Refuse an incomplete prefix rather than
        # silently falling back to the installed AppKit for a candidate test.
        for required in [args.ui_prefix / 'appkit/AppKit.framework/Versions/C/AppKit',
                         args.ui_prefix / 'windowserver/libWindowServer.dylib']:
            if not required.is_file():
                raise FileNotFoundError(required)
        env = private_ui_environment(env, runtime, args.ui_prefix)
    if args.system_coretext:
        frames = [p for p in env['DYLD_FRAMEWORK_PATH'].split(':') if '/coretext/' not in p and '/overrides/memory-font-' not in p]
        env['DYLD_FRAMEWORK_PATH'] = ':'.join(frames + ['/System/Library/Frameworks'])
    if args.observer:
        if not args.observer.is_file():
            raise FileNotFoundError(args.observer)
        if installed_outer:
            env['SFFX_DIAGNOSTIC_OBSERVER'] = str(args.observer)
        else:
            env['DYLD_INSERT_LIBRARIES'] += ':' + str(args.observer)
        env.update(SF_FIREFOX_MEDIA_TRACE='1', SF_FIREFOX_MEDIA_TRACE_SECONDS='60')
    if args.loss_observer:
        if not args.loss_observer.is_file():
            raise FileNotFoundError(args.loss_observer)
        env['SFFX_DIAGNOSTIC_OBSERVER'] += ':' + str(args.loss_observer)
        env['SFFX_TEST_WAYLAND_LOSS'] = '1'
    helpers = module('remote', 'remote-run.py')
    owned = {}
    result = dict(action=args.action,
                  launcher_kind=('installed-outer-launcher' if installed_outer else 'installed-runtime-plan-with-explicit-diagnostic-profile'),
                  installed_outer_or_dock=installed_outer, dock_launch=False, command=command, ui_prefix=str(args.ui_prefix),
                  requested_system_coretext=args.system_coretext, observer=str(args.observer),
                  loss_observer=str(args.loss_observer),
                  owner_input_before=guard.before, audio_stop_qualified=False, complete=False,
                  functional_qa=True, timing_qualified=False)
    client = None
    process = None

    def save():
        result['document'] = fixture.snapshot()
        (args.run / 'result.json').write_text(json.dumps(result, indent=2) + '\n')

    def identity():
        current = helpers.process_identities()
        owned.update({p: current[p][1] for p in helpers.descendants(process.pid, current) if p in current})
        return current

    def pause(seconds):
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            guard.check()
            time.sleep(.05)

    try:
        with (args.run / 'firefox.log').open('w') as log:
            result['launch_wall_seconds'] = time.time()
            process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            result['wrapper_pid'] = process.pid
            end = time.monotonic() + 25
            while time.monotonic() < end:
                guard.check()
                identity()
                if process.poll() is not None:
                    raise RuntimeError('Browser exited before session admission')
                try:
                    client = Marionette(timeout=1)
                    break
                except (OSError, EOFError):
                    time.sleep(.2)
            if client is None:
                raise RuntimeError('Owned Marionette service did not start')
            client.socket.settimeout(25)
            session = client.start('none')
            guard.check()
            client.socket.settimeout(10)
            parent = session['capabilities']['moz:processID']
            current = identity()
            if parent not in owned or current[parent][1] != owned[parent]:
                raise RuntimeError('Marionette parent is outside this owned process family')
            result.update(parent_pid=parent, session=session)
            original = client.command

            def guarded(*argv, **kwargs):
                guard.check()
                reply = original(*argv, **kwargs)
                guard.check()
                return reply

            client.command = guarded
            client.command('Marionette:SetContext', {'value': 'content'})
            until = time.monotonic() + 8
            while not client.command('WebDriver:ExecuteScript', dict(
                    script='return typeof fxLifecycleStart === "function";', args=[], newSandbox=False)):
                if time.monotonic() >= until:
                    raise RuntimeError('Owned audio fixture did not become ready')
                pause(.1)
            started = client.command('WebDriver:ExecuteAsyncScript', dict(script='''
              const done=arguments[arguments.length-1];
              fxLifecycleStart().then(done,e=>done({error:String(e)}));''', args=[], newSandbox=False, scriptTimeout=8000))
            if started.get('error') or started.get('paused') or started.get('muted'):
                raise RuntimeError('Unmuted owned audio did not start: ' + repr(started))
            pause(2)
            from provider_images import inspect
            identity()
            expected = ('/System/Library/Frameworks/AppKit.framework'
                        if manifest.get('system_ui') and not args.ui_prefix
                        else str((args.ui_prefix or runtime) / 'appkit'))
            result['providers'] = inspect(args.sample_path, args.run / 'providers.json', owned, parent, expected,
                                          manifest.get('system_gpu_providers', False) and not args.ui_prefix,
                                          private_windowserver=(str(args.ui_prefix / 'windowserver/libWindowServer.dylib')
                                                                if args.ui_prefix else None))
            if not result['providers']['complete']:
                raise RuntimeError('Actual owned parent/GPU provider check failed')
            # DOM focus alone does not establish AppKit's key window. Native
            # menu validation and keyboard actions need an admitted seat click.
            from presentation_seat import admit, _request
            result['admission'] = admit(client, args.run / 'native-admission.json', fixture=False,
                                        verify_renderer=False, seat_input=not args.ipc_quit_only)
            result['before_windows'] = gecko_windows(client)
            result['before_content_identities'] = {
                str(tab['contentPid']): owned.get(tab['contentPid'])
                for window in result['before_windows'] for tab in window['tabs'] if tab['contentPid']}
            result['before_menu'] = native_menu(parent)
            identity()
            result['before_action_owned_identities'] = {str(pid): start for pid, start in owned.items()}
            if len(result['before_windows']) != 1 or len(result['before_windows'][0]['tabs']) != 1:
                raise RuntimeError('Expected exactly one owned browser window and one audio tab')
            guard.check()
            result['action_wall_ms'] = time.time() * 1000
            result['action_monotonic'] = time.clock_gettime(time.CLOCK_MONOTONIC)
            if args.action == 'connection-loss':
                (args.run / 'wayland-disconnect.trigger').write_text('Disconnect only this admitted private Firefox\n')
                result['native_action'] = dict(own_wayland_shutdown_trigger=True, compositor_restarted=False)
            elif args.action == 'desktop-quit':
                result['native_action'] = dict(route='compositor-app-request', pid=parent,
                    response=_request(Path('/private/var/run/gpu-desktop/inject'), 'app %d quit' % parent))
            elif args.action == 'windowless-quit':
                inject = Path('/private/var/run/gpu-desktop/inject')
                name = 'sffx-lifecycle-%d-prequit' % parent
                _request(inject, 'dump ' + name)
                tx, ty = close_button((inject / (name + '.txt')).read_text(), result['admission']['window_id'])
                result['close_button_point'] = [tx, ty]
                result['close_reply'] = _request(inject,
                    'abs %d %d\nbuttons 1\nwait 60\nbuttons 0' %
                    (round(tx * 65535 / 2559), round(ty * 65535 / 1599)))
                pause(2.5)
                _request(inject, 'dump ' + name)
                result['prequit_native_windows'] = (inject / (name + '.txt')).read_text()
                current = identity()
                if parent not in current or current[parent][1] != owned[parent]:
                    raise RuntimeError('Browser already ended; windowless desktop Quit was not exercised')
                if args.quit_menu_control:
                    menu = native_menu(parent)
                    result['prequit_menu'] = menu
                    items = [m for m in re.finditer(r'^item ([\d.]+) \d+ (\S+) (\S+) (.*)$', menu, re.M)
                             if m[3] == 'cmd+q' and 'd' not in m[2] and 's' not in m[2]]
                    if len(items) != 1:
                        raise RuntimeError('Expected one enabled app-owned Quit item')
                    result['native_action'] = dict(route='app-owned-menu-control', pid=parent,
                        item=items[0][1], title=items[0][4], response=native_menu(parent, 'invoke ' + items[0][1]))
                else:
                    result['native_action'] = dict(route='compositor-app-request', pid=parent,
                        response=_request(inject, 'app %d quit' % parent))
            elif args.action in ['close-box', 'cmd-w', 'minimize']:
                admission = result['admission']
                inject = Path('/private/var/run/gpu-desktop/inject')
                if args.action in ['close-box', 'minimize']:
                    name = 'sffx-lifecycle-%d-close-frame' % parent
                    _request(inject, 'dump ' + name)
                    dump = (inject / (name + '.txt')).read_text()
                    (args.run / 'close-frame.txt').write_text(dump)
                    tx, ty = window_button(dump, admission['window_id'], 'Miniaturize' if args.action == 'minimize' else 'Close')
                    result['close_button_point'] = [tx, ty]
                    lines = 'abs %d %d\nbuttons 1\nwait 60\nbuttons 0' % (round(tx * 65535 / 2559), round(ty * 65535 / 1599))
                else:
                    lines = 'key e3 down\nkey 1a down\nkey 1a up\nkey e3 up'
                result['native_action'] = dict(virtual_seat=True, physical_input=False, response=_request(inject, lines))
            else:
                keys = {'close-tab': 'cmd+w', 'close-window': 'cmd+shift+w', 'hide': 'cmd+h',
                        'minimize': 'cmd+m', 'quit': 'cmd+q'}
                items = [m for m in re.finditer(r'^item ([\d.]+) \d+ (\S+) (\S+) (.*)$', result['before_menu'], re.M)
                         if m[3] == keys[args.action] and 'd' not in m[2] and 's' not in m[2]]
                if len(items) != 1:
                    raise RuntimeError('Expected one enabled native menu item for ' + keys[args.action])
                result['native_action'] = dict(item=items[0][1], title=items[0][4], key=keys[args.action],
                                              response=native_menu(parent, 'invoke ' + items[0][1]))
            save()
            for _ in range(24):
                guard.check()
                identity()
                pause(.25)
            result['exit_before_cleanup'] = process.poll()
            current = identity()
            result['after_action_monotonic'] = time.clock_gettime(time.CLOCK_MONOTONIC)
            result['after_action_owned_alive'] = {
                str(pid): current[pid][1] for pid, start in owned.items()
                if pid in current and current[pid][1] == start}
            if args.action in ['desktop-quit', 'windowless-quit'] and parent in current and current[parent][1] == owned[parent]:
                with (args.run / 'parent-after-quit-sample.txt').open('w') as trace:
                    sample = subprocess.run([args.sample_path, '-p', str(parent), '-d', '0.1', '-i', '5'],
                        stdout=trace, stderr=subprocess.STDOUT, timeout=8)
                result['parent_after_quit_sample_status'] = sample.returncode
            inject = Path('/private/var/run/gpu-desktop/inject')
            from presentation_seat import _request
            _request(inject, 'dump sffx-lifecycle-%d-after' % parent)
            result['after_action_native_windows'] = (inject / ('sffx-lifecycle-%d-after.txt' % parent)).read_text()
            if parent in current and current[parent][1] == owned[parent]:
                result['after_menu'] = native_menu(parent)
                if args.action in ['hide', 'minimize', 'close-tab', 'close-window', 'close-box', 'cmd-w']:
                    # This invokes the compositor's dock/switcher app-action
                    # handler, including its windowless-process routing.
                    result['reopen_route'] = 'compositor-app-request'
                    result['reopen_reply'] = _request(inject, 'app %d reopen' % parent)
                    pause(2)
                    client.command('Marionette:SetContext', {'value': 'chrome'})
                    handles = client.command('WebDriver:GetWindowHandles')
                    if handles:
                        client.command('WebDriver:SwitchToWindow', {'handle': handles[0]})
                    result['after_reopen_windows'] = gecko_windows(client)
                    result['reopen_admission'] = admit(client, args.run / 'reopen-admission.json', fixture=False)
                    _request(inject, 'dump sffx-lifecycle-%d-reopened' % parent)
                    result['after_reopen_native_windows'] = (inject / ('sffx-lifecycle-%d-reopened.txt' % parent)).read_text()
            result['diagnostic_complete'] = True
            result['audio_stop_qualified'] = False  # HAL/callback analysis is a separate evidence gate.
            guard.check()
    except BaseException as error:
        result['error'] = repr(error)
    finally:
        if client is not None:
            client.close()
        if process is not None:
            current = identity()
            result['cleanup_monotonic'] = time.clock_gettime(time.CLOCK_MONOTONIC)
            result['before_cleanup_processes'] = subprocess.run(['ps', '-p', ','.join(map(str, sorted(owned))), '-o', 'pid,ppid,time,stat,command'], capture_output=True, text=True).stdout
            result['owned_pids'] = sorted(owned)
            for sig in [signal.SIGTERM, signal.SIGKILL]:
                current = helpers.process_identities()
                for pid in sorted(owned, reverse=True):
                    if pid in current and current[pid][1] == owned[pid]:
                        try:
                            os.kill(pid, sig)
                        except ProcessLookupError:
                            pass
                time.sleep(.5)
            result['wrapper_exit_after_cleanup'] = process.wait(timeout=5)
            if installed_outer:
                logs = [p for p in (home / 'Library/Logs/SiliconForest/Firefox').glob('firefox-*-%d.log' % process.pid)
                        if p.stat().st_mtime >= result['launch_wall_seconds']-1]
                if len(logs) == 1:
                    shutil.copy2(logs[0], args.run / 'launcher-runtime.log')
                    result['runtime_log_copied'] = True
                else:
                    result['runtime_log_error'] = 'Expected one owned installed-launcher log'
        try:
            result['owner_input_after'] = guard.check()
        except (OSError, RuntimeError) as error:
            result['qualification_invalidated'] = str(error)
        save()
        fixture.close()
    print(json.dumps(result, indent=2))
    return 0 if result.get('diagnostic_complete') and 'qualification_invalidated' not in result else 1


if __name__ == '__main__':
    raise SystemExit(main())
