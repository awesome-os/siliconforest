#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Own headless native control, with a separate broker and bounded own group."""
import json
import os
from pathlib import Path
import signal
import subprocess
import time

root=Path(__file__).resolve().parent
if os.uname().sysname!='Darwin' or root.parent!=Path('/private/var/db/sffx'):
    raise ValueError('Only this cold private M1 test prefix')
if (root/'result.json').exists():raise ValueError('Retain the previous attempt')
env=os.environ.copy()
for name in ['DYLD_FRAMEWORK_PATH','DYLD_FALLBACK_FRAMEWORK_PATH','DYLD_INSERT_LIBRARIES',
             'SFFX_FRAME_ID_GEOMETRY','SFFX_FRAME_ID_DIMENSIONS']:
    env.pop(name,None)
env.update(SFFX_GPU_IMAGE_CACHE='0',SFFX_IOSURFACE_GPU='1',
    SFFX_IOSURFACE_SERVICE='org.siliconforest.iosurfaced.sffx.strictnv12.'+str(os.getpid()),
    SFFX_CGL_PROBE_LIBRARY=str(root/'Frameworks/OpenGL.framework/Versions/A/OpenGL'),
    MESA_SHADER_CACHE_DIR=str(root/'mesa-cache'),XDG_CACHE_HOME=str(root/'cache'),
    DYLD_LIBRARY_PATH='/usr/local/lib/siliconforest/opengl/v1/lib')
command=['/usr/local/lib/siliconforest/firefox/160-system1/surface/bin/surface-session',
         '/usr/libexec/iosurfaced',str(root/'nv12-plane-probe')]
result=dict(command=command,cleanup_signals=[],input_injected=False,installed_changes=False,
    service=env['SFFX_IOSURFACE_SERVICE'])
def kernel_snapshot():
    snapshot=subprocess.run(['sysctl','kern.uuid','kern.boottime','kern.sf_dri.clone_slots'],
        capture_output=True,text=True,timeout=3)
    return dict(returncode=snapshot.returncode,stdout=snapshot.stdout,stderr=snapshot.stderr,
        scope='Global read-only context; other clients can change clone totals')
result['kernel_before']=kernel_snapshot()
before=time.monotonic()
with (root/'native.log').open('w') as log:
    child=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
    result['pid']=child.pid
    result['start_identity']=subprocess.run(['ps','-p',str(child.pid),'-o','lstart='],
        capture_output=True,text=True,timeout=3).stdout.strip()
    try:child.wait(timeout=45)
    except subprocess.TimeoutExpired:
        result['timeout']=True
        # start_new_session created an isolated group containing only this
        # wrapper, its test and its private broker. Never signal a desktop app.
        for sig in [signal.SIGTERM,signal.SIGKILL]:
            if child.poll() is not None:break
            if os.getpgid(child.pid)!=child.pid:raise RuntimeError('Own group changed')
            os.killpg(child.pid,sig);result['cleanup_signals'].append(int(sig))
            try:child.wait(timeout=2);break
            except subprocess.TimeoutExpired:pass
    result['status']=child.poll()
rows=subprocess.check_output(['ps','-axo','pid=,pgid=,stat=,command='],text=True,timeout=3)
result['remaining_group']=[line.strip() for line in rows.splitlines()
    if len(line.split(None,3))==4 and int(line.split(None,3)[1])==child.pid
    and not line.split(None,3)[2].startswith('Z')]
result['elapsed_seconds']=time.monotonic()-before
result['kernel_after']=kernel_snapshot()
(root/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
raise SystemExit(0 if result['status']==0 and not result['cleanup_signals'] and not result['remaining_group'] else 1)
