#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Bounded own VLC CLI test; no window, capture or synthetic input."""
import importlib.util,json,os,signal,subprocess,time
from pathlib import Path


def main():
    root=Path.cwd().resolve()
    if os.uname().sysname!='Darwin' or not str(root).startswith('/private/var/db/sffinch/'):
        raise ValueError('A fresh reviewed M1 staging directory is required')
    spec=importlib.util.spec_from_file_location('own_vlc_launch',root/'launch.py')
    api=importlib.util.module_from_spec(spec);spec.loader.exec_module(api)
    report=dict(player_started=False,abort=None,signals_to_own_child=[],
                scope='Direct CLI; no open-a, window, capture, pointer or key input.')
    try:guard=api.OwnerInputGuard()
    except (OSError,RuntimeError) as error:
        report.update(abort='OWNER_IDLE_NOT_ESTABLISHED',reason=str(error))
        (root/'direct-player.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(report),flush=True);return 3
    state=root/'state';state.mkdir()
    env=dict(os.environ,SF_VLC_LOG=str(state/'vlc.log'),SF_VLC_HAL_LOG='1',SF_VLC_HEADLESS_TIMEOUT='1')
    argv=[str(root/'Applications/VLC.app/Contents/MacOS/SiliconForest-VLC'),
          '--videotoolbox-hw-decoder-only','--intf','dummy','--extraintf','oldrc',
          '--rc-unix',str(state/'rc.sock'),'--rc-fake-tty','--vout','dummy',
          '--aout','auhal','--codec','videotoolbox,avcodec','--ignore-config',
          '--no-plugins-cache','--no-media-library','--no-metadata-network-access',
          '--play-and-exit','-vv',str(root/'fixtures/movie.mov')]
    child=None
    try:
        # Files avoid a verbose VLC filling an undrained stdout/stderr pipe.
        with (root/'player.stdout').open('wb') as out,(root/'player.stderr').open('wb') as err:
            guard.check()
            child=subprocess.Popen(argv,env=env,stdout=out,stderr=err,start_new_session=True)
            report.update(player_started=True,argv=argv,pid=child.pid,owner_before=guard.before)
            deadline=time.monotonic()+12
            while child.poll() is None:
                guard.check()
                if time.monotonic()>deadline:raise TimeoutError('Own player exceeded12s')
                time.sleep(.05)
            report['owner_after']=guard.check()
    except (OSError,RuntimeError,TimeoutError) as error:
        report.update(abort=type(error).__name__,reason=str(error))
    finally:
        if child:
            if child.poll() is None:
                # Popen created this session; never locate or signal any other
                # decoder, application, broker or owner process.
                for number in [signal.SIGTERM,signal.SIGKILL]:
                    try:os.killpg(child.pid,number)
                    except ProcessLookupError:break
                    report['signals_to_own_child'].append(dict(pid=child.pid,signal=int(number)))
                    try:child.wait(timeout=2);break
                    except subprocess.TimeoutExpired:pass
            report['returncode']=child.wait(timeout=2)
    for name in ['player.stdout','player.stderr','state/vlc.log']:
        path=root/name;report[name]=path.read_text(errors='replace') if path.exists() else ''
    (root/'direct-player.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report),flush=True)
    return int(bool(report['abort'] or report.get('returncode')!=0))


if __name__=='__main__':raise SystemExit(main())
