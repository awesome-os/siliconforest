#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Finite staged VLC launch through open -a. No screen input; optional same-uid capture."""
import argparse,json,os,subprocess,time,pwd,hashlib,re,ctypes,socket
from pathlib import Path


def owner_snapshot(path=Path('/private/var/run/gpu-desktop/owner-input'),now_ms=None):
    """DESK7's actual marker; never infer idle from an absent/stale startup."""
    values={key:int(value) for key,value in re.findall(r'^([a-z_]+)=(\d+)$',path.read_text(),re.M)}
    required={'active','window_ms','last_real_input_unix_ms','real_pointer_records','real_key_records','updated_unix_ms'}
    if not required<=values.keys():raise RuntimeError('Malformed owner-input marker')
    now=time.time()*1000 if now_ms is None else now_ms
    last=values['last_real_input_unix_ms'] or values['updated_unix_ms']
    if (values['active']!=0 or values['window_ms']<10000 or
        not 0<values['updated_unix_ms']<=now+1500 or
        not 0<last<=now or now-last<values['window_ms']):
        raise RuntimeError('Owner active or idle window not established')
    return values


class OwnerInputGuard:
    def __init__(self,path=Path('/private/var/run/gpu-desktop/owner-input'),now_ms=None):
        self.path=path;self.before=owner_snapshot(path,now_ms)

    def check(self,now_ms=None):
        after=owner_snapshot(self.path,now_ms)
        if any(after[key]!=self.before[key] for key in
               ['last_real_input_unix_ms','real_pointer_records','real_key_records']):
            raise RuntimeError('Real owner input observed during this run')
        return after


def stop_owned_entry(work):
    """Only PIDs with this fresh stage's exact executable path, even before main."""
    log=work/'state/vlc.log'
    match=re.search(r'^SF_VLC_ENTRY pid=(\d+)\b',log.read_text(errors='replace') if log.exists() else '',re.M)
    pids={int(match[1])} if match else set()
    # A cold process can exist before main has written its entry log. Do not
    # leave it behind if real input interrupts dyld or the open helper.
    listing=subprocess.run(['ps','-A','-o','pid='],capture_output=True,text=True,timeout=1)
    pids.update(int(line.strip()) for line in listing.stdout.splitlines() if line.strip().isdigit())
    expected=str(work/'Applications/VLC.app/Contents/MacOS/SiliconForest-VLC')
    lib=ctypes.CDLL('/usr/lib/libSystem.B.dylib')
    lib.proc_pidpath.argtypes=[ctypes.c_int,ctypes.c_void_p,ctypes.c_uint32];lib.proc_pidpath.restype=ctypes.c_int
    def owned(pid):
        buf=ctypes.create_string_buffer(4096)
        return pid>1 and lib.proc_pidpath(pid,buf,len(buf))>0 and buf.value.decode(errors='replace')==expected
    sent=[]
    for pid in sorted(pids):
        for number in [15,9]:
            if not owned(pid):break
            try:os.kill(pid,number)
            except ProcessLookupError:break
            sent.append(dict(pid=pid,signal=number))
            until=time.monotonic()+2
            while owned(pid) and time.monotonic()<until:time.sleep(.05)
    return sent


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--framework-prefix',action='store_true',help='Use only reviewed Frameworks/lib inside this stage')
    ap.add_argument('--hardware',action='store_true',help='Prefer actual VideoToolbox with its require-hardware option')
    ap.add_argument('--capture',action='store_true')
    ap.add_argument('--no-statusicon',action='store_true',help='Disable VLC optional status-menu icon while AppKit lacks its backend')
    ap.add_argument('--as-user',help='Existing unprivileged account; no account or global privilege changes')
    ap.add_argument('--audio-prefix',action='store_true',help='Use the reviewed AudioToolbox-only prefix inside this stage')
    ap.add_argument('--headless',action='store_true',help='VLC dummy interface/video plus real auhal; no GUI evidence')
    a=ap.parse_args()
    if a.headless and a.capture:raise ValueError('A headless run cannot produce a player-window capture')
    work=Path(__file__).resolve().parent
    if os.uname().sysname!='Darwin' or not str(work).startswith('/private/var/db/sffinch/'):
        raise ValueError('Run only from the reviewed M1 staging directory')
    guard=None if a.headless else OwnerInputGuard()
    def owner_check():
        if guard:
            try:guard.check()
            except (OSError,RuntimeError) as error:
                signals=stop_owned_entry(work)
                (work/'owner-input-abort.json').write_text(json.dumps(dict(reason=str(error),signals_to_verified_own_pid=signals))+'\n')
                raise
    env={k:v for k,v in os.environ.items() if not k.startswith('DYLD_')}
    env.update(XDG_RUNTIME_DIR='/private/var/run/gpu-desktop',WAYLAND_DISPLAY='wayland-0',SF_METAL_ENABLE_REAL_DEVICE='0')
    if a.framework_prefix:
        if not (work/'Frameworks').is_dir():raise ValueError('Missing own Frameworks prefix')
        env['DYLD_FRAMEWORK_PATH']=str(work/'Frameworks')
        if (work/'lib').is_dir():env['DYLD_LIBRARY_PATH']=str(work/'lib')
    if a.audio_prefix:
        prefix=work/'audio-prefix/System/Library/Frameworks'
        if not (prefix/'AudioToolbox.framework/AudioToolbox').is_file():
            raise ValueError('Missing reviewed own AudioToolbox prefix')
        env['DYLD_FRAMEWORK_PATH']=str(prefix)
    for record in json.loads((work/'bundle-hashes.json').read_text()):
        path=work/record['path']
        if not path.resolve().is_relative_to(work):raise ValueError('Bundle path escapes own stage')
        if hashlib.file_digest(path.open('rb'),'sha256').hexdigest()!=record['sha256']:
            raise ValueError('Cold bundle hash mismatch: '+record['path'])
    drop=None
    state=work/'state';state.mkdir(exist_ok=True)
    if a.as_user:
        account=pwd.getpwnam(a.as_user)
        if account.pw_uid==0:raise ValueError('VLC requires an unprivileged account')
        os.chown(state,account.pw_uid,account.pw_gid);state.chmod(0o700)
        def drop():
            os.setgroups([]);os.setgid(account.pw_gid);os.setuid(account.pw_uid)
    argv=['open' ,'-a',str(work/'Applications/VLC.app'),'--args','--ignore-config','--no-plugins-cache',
          '--no-media-library','--no-metadata-network-access','--play-and-exit',
          '-vv',str(work/'fixtures/movie.mov')]
    if not a.headless:argv[4:4]=['--intf','macosx']
    if a.no_statusicon:argv[4:4]=['--no-macosx-statusicon']
    if a.headless:argv[4:4]=['--intf','dummy','--extraintf','oldrc','--rc-unix',str(state/'rc.sock'),'--rc-fake-tty','--vout','dummy','--aout','auhal','--codec','avcodec']
    if a.hardware:
        if a.headless:argv[argv.index('avcodec')]='videotoolbox,avcodec'
        else:argv[4:4]=['--codec','videotoolbox,avcodec']
        argv[4:4]=['--videotoolbox-hw-decoder-only']
    env['SF_VLC_LOG']=str(state/'vlc.log')
    env['SF_VLC_HAL_LOG']='1'
    if os.environ.get('SF_VLC_TEST_FIRST_RUN_CONFIGURED')=='1':
        env['SF_VLC_TEST_FIRST_RUN_CONFIGURED']='1'
    if a.headless:env['SF_VLC_HEADLESS_TIMEOUT']='1'
    with (work/'open.stdout').open('w') as out,(work/'open.stderr').open('w') as err:
        owner_check()
        r=subprocess.run(argv,env=env,stdout=out,stderr=err,timeout=15,preexec_fn=drop)
    rc_stats=[]
    if a.headless:
        # Read the actual vendor control interface's statistics. This is an
        # owned CLI socket, no screen input and no playback-changing command.
        deadline=time.monotonic()+22
        while time.monotonic()<deadline:
            if (state/'rc.sock').exists() and len(rc_stats)<12:
                try:
                    with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as control:
                        control.settimeout(0.2);control.connect(str(state/'rc.sock'))
                        control.sendall(b'stats\n');data=bytearray()
                        while len(data)<65536:
                            try:
                                block=control.recv(min(8192,65536-len(data)))
                            except socket.timeout:break
                            if not block:break
                            data.extend(block)
                        rc_stats.append({'elapsed_until_deadline':round(deadline-time.monotonic(),3),'stdout':data.decode(errors='replace')})
                except OSError:pass
            time.sleep(0.25)
    else:
        deadline=time.monotonic()+8
        while time.monotonic()<deadline:owner_check();time.sleep(.05)
    listing=subprocess.run(['ps','-A','-o','pid=','-o','args='],capture_output=True,text=True,check=True).stdout
    own=[line for line in listing.splitlines() if str(work/'Applications/VLC.app/Contents/MacOS/VLC') in line]
    entry_pid=None;entry_identity=None
    logfile=state/'vlc.log'
    match=re.search(r'SF_VLC_ENTRY pid=(\d+)',logfile.read_text(errors='replace') if logfile.exists() else '')
    if match:
        entry_pid=int(match[1])
        row=next((line for line in listing.splitlines() if line.strip().split(None,1)[0]==str(entry_pid)),None)
        entry_identity={'pid':entry_pid,'ps_line':row,'path':None}
        try:
            lib=ctypes.CDLL('/usr/lib/libSystem.B.dylib');lib.proc_pidpath.argtypes=[ctypes.c_int,ctypes.c_void_p,ctypes.c_uint32];lib.proc_pidpath.restype=ctypes.c_int
            buf=ctypes.create_string_buffer(4096);count=lib.proc_pidpath(entry_pid,buf,len(buf))
            if count>0:entry_identity['path']=buf.value.decode(errors='replace')
        except Exception as error:entry_identity['path_error']=str(error)
        if row and entry_identity.get('path')==str(work/'Applications/VLC.app/Contents/MacOS/SiliconForest-VLC') and row not in own:own.append(row)
    capture=None
    if a.capture and own:
        owner_check()
        p=subprocess.run(['/usr/bin/chonkstep-darwin-desktop','--screencapture',str(work/'vlc.png')],env=env,capture_output=True,text=True,timeout=20)
        owner_check()
        capture={'returncode':p.returncode,'stdout':p.stdout,'stderr':p.stderr,'file_exists':(work/'vlc.png').exists()}
    report={'open_argv':argv,'open_returncode':r.returncode,'vlc_process_lines':own,'capture':capture,'entry_identity':entry_identity,
            'logs':{name:(work/name).read_text(errors='replace') if (work/name).exists() else None
                    for name in ['open.stdout','open.stderr','state/vlc.log']}, 'launch_user':a.as_user or 'current', 'audio_prefix':str(prefix) if a.audio_prefix else None, 'vendor_rc_stats':rc_stats,
            'framework_prefix':a.framework_prefix, 'hardware_requested':a.hardware, 'status_icon_disabled':a.no_statusicon, 'mode':'headless dummy video/real auhal' if a.headless else 'GUI attempt',
            'note':'Process presence/open success is not playback. No input injection or session restart. No other process was signalled.'}
    (work/'launch.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
