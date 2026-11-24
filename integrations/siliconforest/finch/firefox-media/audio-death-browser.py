#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Disconnect only an admitted own Firefox client while its audio is active.

No input injection, menu invocation, compositor restart or owner-profile access.
Server slot logs are observations; concurrent clients can make attribution
ambiguous, so they never automatically qualify an acoustic/audio-stop result.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import time


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--run',type=Path,required=True)
    p.add_argument('--audio-clock-family',action='store_true',help='Private full AudioToolbox comparison, never changes installed selection')
    p.add_argument('--state-controls',action='store_true',help='Own media volume/mute/pause/two-tab controls and normal quit, no disconnect')
    p.add_argument('--expect-installed-clock',action='store_true',help='Require the selected installed normalized AudioToolbox family, never override it')
    p.add_argument('--watch-server-client',action='store_true',help='Read-only exact own shared-ring server-slot removal; installed clock only')
    a=p.parse_args()
    if a.expect_installed_clock and (not (a.state_controls or a.watch_server_client) or a.audio_clock_family):
        raise ValueError('Installed clock gate requires state controls or exact client watch, no private selection')
    if a.watch_server_client and (not a.expect_installed_clock or a.state_controls):
        raise ValueError('Exact client watch requires installed-clock abnormal exit, not state controls')
    root=Path(__file__).resolve().parent
    if os.uname().sysname!='Darwin' or not root.is_relative_to(Path('/private/var/db/sffx')):
        raise ValueError('Cold private SiliconForest packet required')
    if not a.run.is_absolute() or '..' in a.run.parts or not a.run.is_relative_to(root) or a.run.exists():
        raise ValueError('Fresh owned destination required')
    installed_clock=Path('/usr/local/lib/siliconforest/firefox/160-system1/overrides/audio-clock-v1/Frameworks')
    selected_manifest=None
    if a.expect_installed_clock:
        selected_manifest=json.loads(Path('/usr/local/lib/siliconforest/firefox/160-system1/release.json').read_text())
        if selected_manifest.get('audio_output_clock') is not True or selected_manifest.get('audio_override')!=str(installed_clock):
            raise ValueError('Installed normalized audio family is not selected; no test launched')
    sys.path.insert(0,str(root/'tools'))
    from marionette import Marionette
    from lifecycle_observer import AudioFixture
    spec=importlib.util.spec_from_file_location('own_remote',root/'tools/remote-run.py')
    helpers=importlib.util.module_from_spec(spec);spec.loader.exec_module(helpers)
    a.run.mkdir();profile=a.run/'profile';profile.mkdir();home=a.run/'home';home.mkdir()
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1',0));port=reservation.getsockname()[1]
    prefs={'marionette.port':port,'media.autoplay.default':0,'termsofuse.bypassNotification':True,
           'browser.shell.checkDefaultBrowser':False,'browser.aboutwelcome.enabled':False,
           'browser.preonboarding.enabled':False,'app.update.auto':False,'browser.tabs.inTitlebar':0,
           'webgl.disable-angle':True,'dom.ipc.processPrelaunch.enabled':False}
    (profile/'user.js').write_text(''.join('user_pref(%s,%s);\n'%(json.dumps(k),json.dumps(v)) for k,v in prefs.items()))
    fixture=AudioFixture(a.run);owned={};client=None;process=None;watch=None
    server=Path('/var/log/coreaudiod.log');offset=0;inode=None;server_rows=[]
    result=dict(synthetic_input=False,installed_outer_launcher=not a.audio_clock_family,audio_stop_qualified=False,
                normalized_audio_family=a.audio_clock_family,
                expected_installed_clock=a.expect_installed_clock,selected_manifest=selected_manifest,
                acoustic_stop_proved=False,cleanup_signals=[],timeline=[],port=port)
    def save():
        result['document']=fixture.snapshot();result['coreaudiod_observations']=server_rows
        (a.run/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    def identities():
        current=helpers.process_identities()
        if process:
            owned.update({pid:current[pid][1] for pid in helpers.descendants(process.pid,current) if pid in current})
        return current
    def observe():
        nonlocal inode,offset
        current=identities();result['timeline'].append(dict(monotonic=time.monotonic(),epoch=time.time(),
            own_alive=[pid for pid,start in owned.items() if pid in current and current[pid][1]==start],
            launcher_exit=process.poll() if process else None))
        try:
            st=server.stat()
            if inode!=st.st_ino or st.st_size<offset:
                server_rows.append(dict(monotonic=time.monotonic(),rotation=True));inode=st.st_ino;offset=0
            with server.open('rb') as f:
                f.seek(offset);data=f.read();offset=f.tell()
            if data:server_rows.append(dict(monotonic=time.monotonic(),text=data.decode(errors='replace')))
        except OSError as e:result['server_log_error']=str(e)
    def pause(seconds):
        until=time.monotonic()+seconds
        while time.monotonic()<until:observe();time.sleep(.1)
    try:
        if server.exists():st=server.stat();inode=st.st_ino;offset=st.st_size
        result['coreaudiod_start']=dict(inode=inode,offset=offset)
        observers=[root/'lib/SFFirefoxMediaObserver.dylib',root/'lib'/('SFHALPCMTrace.dylib' if a.state_controls else 'SFFirefoxWaylandLoss.dylib')]
        if not all(path.is_file() for path in observers):raise ValueError('Both Linux-built observers required')
        env=os.environ.copy();env.update(SFFX_DIAGNOSTIC_OBSERVER=':'.join(map(str,observers)),
            SFFX_TEST_WAYLAND_LOSS='1',SF_FIREFOX_MEDIA_TRACE='1',SF_FIREFOX_MEDIA_TRACE_SECONDS='50',
            MOZ_REMOTE_ALLOW_SYSTEM_ACCESS='1',MOZ_NO_REMOTE='1',MOZ_CRASHREPORTER_DISABLE='1',
            HOME=str(home),CFFIXED_USER_HOME=str(home))
        if a.state_controls:
            env.pop('SFFX_TEST_WAYLAND_LOSS',None)
            env.update(SF_HAL_PCM_AUDIT='1',SF_HAL_PCM_TRACE='1',
                       SFFX_HAL_PCM_AUDIT='1',SFFX_HAL_PCM_TRACE='1')
        arguments=['--no-remote','--profile',str(profile),'--marionette',fixture.url]
        if a.audio_clock_family:
            if not (root/'Frameworks/AudioToolbox.framework').is_dir():raise ValueError('Full staged AudioToolbox family required')
            command=[sys.executable,str(root/'media-launch.py')]+arguments
            env['SF_AU_NORMALIZE_HAL_CLOCK']='1'
        else:command=['/Applications/Firefox.app/Contents/MacOS/firefox-sf']+arguments
        with (a.run/'firefox.log').open('w') as out:
            process=subprocess.Popen(command,env=env,stdout=out,stderr=subprocess.STDOUT,start_new_session=True)
            result.update(command=command,launcher_pid=process.pid,launch_monotonic=time.monotonic())
            until=time.monotonic()+25
            while time.monotonic()<until:
                observe()
                if process.poll() is not None:raise RuntimeError('Own launcher exited before automation')
                try:client=Marionette(port=port,timeout=1);break
                except (OSError,EOFError):time.sleep(.2)
            if client is None:raise RuntimeError('Own automation port unavailable')
            client.socket.settimeout(12);session=client.start('none');parent=int(session['capabilities']['moz:processID']);current=identities()
            if parent not in owned or current[parent][1]!=owned[parent]:
                client.socket.close();client=None;raise RuntimeError('Refuse automation outside owned launch')
            result.update(parent_pid=parent,session=session)
            client.command('Marionette:SetContext',{'value':'content'})
            until=time.monotonic()+8
            while not client.command('WebDriver:ExecuteScript',dict(script='return typeof fxLifecycleStart === "function";',args=[],newSandbox=False)):
                if time.monotonic()>until:raise RuntimeError('Owned audio fixture not ready')
                pause(.1)
            result['audio_started']=client.command('WebDriver:ExecuteAsyncScript',dict(
                script='const done=arguments[arguments.length-1]; fxLifecycleStart().then(done,e=>done({error:String(e)}));',
                args=[],newSandbox=False,scriptTimeout=8000))
            if result['audio_started'].get('error') or result['audio_started'].get('paused') or result['audio_started'].get('muted'):
                raise RuntimeError('Unmuted own document did not start')
            if a.audio_clock_family or a.expect_installed_clock:
                import media_images
                directory=a.run/'media-providers';directory.mkdir()
                result['media_providers']=media_images.inspect(
                    [dict(pid=parent,type='browser',os_start_identity=owned[parent])],directory,
                    installed_clock if a.expect_installed_clock else root/'Frameworks')
                if not result['media_providers']['complete']:raise RuntimeError('Full normalized AudioToolbox family not selected')
            if a.state_controls:
                import audio_state_controls
                result['state_controls']=audio_state_controls.run(client,fixture.url,pause)
                result['action_monotonic']=time.monotonic();result['action_epoch']=time.time()
                try:result['quit_reply']=client.command('Marionette:Quit',{'flags':[]})
                except (OSError,EOFError) as error:result['quit_transport_end']=type(error).__name__
            else:
                pause(2)
                if a.watch_server_client:
                    from audio_session_watch import Watch
                    watch=Watch(root,a.run,parent,owned[parent],identities,pause)
                    result['audio_session_watch_admission']=watch.result
                result['action_monotonic']=time.monotonic();result['action_epoch']=time.time()
                (a.run/'wayland-disconnect.trigger').write_text('Disconnect only this admitted own Firefox client\n')
            pause(8);result['exit_before_cleanup']=process.poll();result['before_cleanup_owned']=result['timeline'][-1]['own_alive']
            if watch:
                result['audio_session_watch']=watch.finish(result['action_epoch'])
                result['server_slot_removal_qualified']=result['audio_session_watch'].get('qualified',False)
            result['diagnostic_complete']=True;save()
    except Exception as error:result['error']=repr(error)
    finally:
        if client is not None:client.close()
        result['cleanup_monotonic']=time.monotonic()
        if process:
            for number in [signal.SIGTERM,signal.SIGKILL]:
                current=identities()
                for pid,start in sorted(owned.items(),reverse=True):
                    if pid in current and current[pid][1]==start:
                        try:os.kill(pid,number);result['cleanup_signals'].append(dict(pid=pid,signal=int(number)))
                        except ProcessLookupError:pass
                try:process.wait(timeout=2)
                except subprocess.TimeoutExpired:pass
            result['launcher_final_exit']=process.poll()
        try:
            import audio_logs
            result['native_log_files']=audio_logs.collect(a.run,home)
        except (OSError,ValueError) as error:result['native_log_collection_error']=repr(error)
        observe();save();fixture.close()
    return int(bool(result.get('error') or result.get('native_log_collection_error') or
                    not result.get('diagnostic_complete') or result.get('before_cleanup_owned') or
                    (a.watch_server_client and not result.get('server_slot_removal_qualified'))))


if __name__=='__main__':raise SystemExit(main())
