#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Finite direct QuickTime launch, own stdout/stderr and exact-process cleanup."""
import hashlib,json,os,resource,signal,subprocess,time
from pathlib import Path
root=Path(__file__).resolve().parent
if os.uname().sysname!='Darwin' or not str(root).startswith('/private/var/db/sffinch/'):
 raise ValueError('M1 staged execution only')
verified=[]
for row in json.loads((root/'cold-files.json').read_text()):
 p=root/row['path']
 if not p.resolve().is_relative_to(root):raise ValueError('Stage path escape')
 if hashlib.file_digest(p.open('rb'),'sha256').hexdigest()!=row['sha256']:raise ValueError('Cold hash mismatch: '+row['path'])
 verified.append(row)
(root/'cold-verified.json').write_text(json.dumps(verified,indent=2)+'\n')
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
env={k:v for k,v in os.environ.items() if not k.startswith('DYLD_')}
env.update(DYLD_FRAMEWORK_PATH=str(root/'System/Library/PrivateFrameworks')+':'+str(root/'System/Library/Frameworks'),XDG_RUNTIME_DIR='/private/var/run/gpu-desktop',WAYLAND_DISPLAY='wayland-0',SF_METAL_ENABLE_REAL_DEVICE='0')
exe=root/'Applications/QuickTime Player.app/Contents/MacOS/QuickTime Player'
start=time.monotonic();process=subprocess.Popen([str(exe)],cwd=root,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True)
timed=False;signals=[]
try:out,err=process.communicate(timeout=20)
except subprocess.TimeoutExpired:
 timed=True;process.send_signal(signal.SIGTERM);signals.append('TERM')
 try:out,err=process.communicate(timeout=2)
 except subprocess.TimeoutExpired:process.kill();signals.append('KILL');out,err=process.communicate(timeout=2)
report=dict(argv=[str(exe)],pid=process.pid,returncode=process.returncode,timeout=timed,signals_to_owned_pid=signals,seconds=round(time.monotonic()-start,3),stdout=out.decode(errors='replace'),stderr=err.decode(errors='replace'),framework_prefix=env['DYLD_FRAMEWORK_PATH'],limits='Direct executable load only; no input, no screen capture, no compositor/app restart, no other process signalled; survival does not prove a window or playback.')
(root/'load.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
