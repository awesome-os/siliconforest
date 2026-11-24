#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Capture-only own VLC run; abort on real input and clean only our exact PID."""
import ctypes,importlib.util,json,os,re,signal,subprocess,sys,time
from pathlib import Path


def main():
    root=Path(__file__).resolve().parent
    if os.uname().sysname!='Darwin' or not str(root).startswith('/private/var/db/sffinch/'):
        raise ValueError('Own reviewed M1 staging directory required')
    spec=importlib.util.spec_from_file_location('own_vlc_launch',root/'launch.py')
    api=importlib.util.module_from_spec(spec);spec.loader.exec_module(api)
    guard=api.OwnerInputGuard()
    lib=ctypes.CDLL('/usr/lib/libSystem.B.dylib')
    lib.proc_pidpath.argtypes=[ctypes.c_int,ctypes.c_void_p,ctypes.c_uint32];lib.proc_pidpath.restype=ctypes.c_int
    expected=str(root/'Applications/VLC.app/Contents/MacOS/SiliconForest-VLC')
    def owned_pid():
        log=root/'state/vlc.log'
        match=re.search(r'^SF_VLC_ENTRY pid=(\d+)\b',log.read_text(errors='replace') if log.exists() else '',re.M)
        if not match:return None
        pid=int(match[1]);buf=ctypes.create_string_buffer(4096)
        if pid>1 and lib.proc_pidpath(pid,buf,len(buf))>0 and buf.value.decode(errors='replace')==expected:return pid
        return None
    failure=None;signals=[];launcher=None;launcher_return=None;owner_after=None
    try:
        with (root/'gui-launch.stdout').open('w') as out,(root/'gui-launch.stderr').open('w') as err:
            launcher=subprocess.Popen([sys.executable,'launch.py','--framework-prefix','--hardware','--capture'],cwd=root,stdout=out,stderr=err,start_new_session=True)
            deadline=time.monotonic()+55
            while launcher.poll() is None:
                guard.check()
                if time.monotonic()>deadline:raise TimeoutError('Own launch helper timed out')
                time.sleep(.05)
            launcher_return=launcher.returncode
            if launcher_return:raise RuntimeError('Own launch helper exited '+str(launcher_return))
        # Capture was at8s; allow the30-second own clip to finish naturally.
        deadline=time.monotonic()+40
        while owned_pid():
            guard.check()
            if time.monotonic()>deadline:break
            time.sleep(.05)
        owner_after=guard.check()
    except (OSError,RuntimeError,TimeoutError) as error:
        failure=str(error)
    finally:
        if launcher and launcher.poll() is None:
            try:os.killpg(launcher.pid,signal.SIGTERM)
            except ProcessLookupError:pass
            try:launcher.wait(timeout=2)
            except subprocess.TimeoutExpired:pass
            # This group was created by our Popen, including its open helper.
            # Finish those descendants too, even if the Python leader exited.
            try:os.killpg(launcher.pid,signal.SIGKILL)
            except ProcessLookupError:pass
            launcher.wait(timeout=2)
            launcher_return=launcher.returncode
        signals=api.stop_owned_entry(root)
    report=json.loads((root/'launch.json').read_text()) if (root/'launch.json').exists() else None
    log=(root/'state/vlc.log').read_text(errors='replace') if (root/'state/vlc.log').exists() else ''
    state=dict(launcher_returncode=launcher_return,failure=failure,
               verified_own_entry_remaining=owned_pid(),signals_to_verified_own_entry=signals,
               logged_exit_statuses=re.findall(r'SF_VLC_STAGE exit-status=(-?\d+)',log),
               owner_before=guard.before,owner_after=owner_after,
               process_local_first_run_profile=os.environ.get('SF_VLC_TEST_FIRST_RUN_CONFIGURED')=='1',
               scope='No pointer/key injection, no restart/install, only our exact staged process can be signalled.')
    (root/'owned-player-lifecycle.json').write_text(json.dumps(state,indent=2)+'\n')
    print('OWN_GUI_LIFECYCLE '+json.dumps(state),flush=True)
    print('OWN_GUI_LOG '+json.dumps(log),flush=True)
    print('OWN_GUI_LAUNCH '+json.dumps(report),flush=True)
    return int(bool(failure or state['verified_own_entry_remaining']))


if __name__=='__main__':raise SystemExit(main())
