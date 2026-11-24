#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fresh strict decoder/GPU import packet for the kernel owner's native gate.

Uses FX's ONE NV12 IOSurface/broker family, not another surface implementation.
All inputs are Linux-built outputs or our generated movies/references.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

p=argparse.ArgumentParser(description=__doc__)
for name in ['surface','providers','probe','fixtures','stage']:
    p.add_argument('--'+name,type=Path,required=True)
p.add_argument('--capture-buffers',type=int,choices=range(1,33),help='Private bounded-pool measurement; codec minimum still applies')
p.add_argument('--hold-final',action='store_true',help='Keep the final picture through session invalidation; no GPU-fence claim')
p.add_argument('--retire-pool',action='store_true',help='Private opt-in pool retirement; requires the held-picture control')
p.add_argument('--gpu-final',action='store_true',help='Verify a real GPU copy of the last picture after invalidation and picture release')
p.add_argument('--graphics-override',type=Path,help='FX-qualified versioned graphics family; required for GPU final-picture verification')
a=p.parse_args();here=Path(__file__).resolve().parent
if a.retire_pool and not a.hold_final:raise ValueError('Retirement requires the held-picture control')
if a.gpu_final and (not a.hold_final or not a.graphics_override):raise ValueError('GPU verification requires held picture and explicit graphics family')
if a.graphics_override and (not a.gpu_final or '..' in a.graphics_override.parts or
    not str(a.graphics_override).startswith('/usr/local/lib/siliconforest/firefox/160-system1/overrides/')):
    raise ValueError('A qualified versioned Firefox graphics family is required')
if a.stage.exists():raise ValueError('Fresh stage required')
a.stage.mkdir();(a.stage/'Frameworks').mkdir();(a.stage/'bin').mkdir();(a.stage/'fixtures').mkdir()
for family in ['CoreVideo','VideoToolbox']:
    shutil.copytree(a.providers/'Frameworks'/(family+'.framework'),
                    a.stage/'Frameworks'/(family+'.framework'),symlinks=True)
shutil.copytree(a.surface/'Frameworks/IOSurface.framework',a.stage/'Frameworks/IOSurface.framework',symlinks=True)
for name in ['surface-session','iosurfaced']:
    shutil.copy2(a.surface/'bin'/name,a.stage/'bin'/name)
shutil.copy2(a.probe/'bin/capture-probe',a.stage/'bin/capture-probe')
for path in a.stage.rglob('*'):
    if not path.is_symlink():path.chmod(path.stat().st_mode|0o200)
subprocess.run(['python3',str(here.parent/'tools/portable-tests.py'),
    str(here.parents[1]/'homebrew/hb_macho.py'),str(a.stage/'bin'),str(a.stage/'portable-bin.json')],check=True)
shutil.copy2(here.parent/'tools/compare-tests.py',a.stage/'compare-tests-engine.py')
shutil.copy2(here/'cold-capture.py',a.stage/'compare-tests.py')
shutil.copy2(here/'shared-capture-case.py',a.stage/'shared-capture-case.py')
manifest=[]
for clip in ['pattern-1080p60-h264','pattern-4k30-hevc']:
    for extension in ['.mp4','.framemd5']:
        shutil.copy2(a.fixtures/(clip+extension),a.stage/'fixtures'/(clip+extension))
    manifest.append(dict(name=clip,mode='exit',skip_lines=0,timeout=75,
        argv=['@PYTHON@','shared-capture-case.py',clip],env={
            'DYLD_FRAMEWORK_PATH':'@ROOT@/Frameworks','SFFX_IOSURFACE_GPU':'1',
            'SF_VT_ZERO_COPY':'require','VT_AVD_TIMING':'1',
            'SFFX_IOSURFACE_SERVICE':'org.siliconforest.iosurfaced.sffx.codex.strict.'+clip}))
shutil.copy2(a.fixtures/'manifest.json',a.stage/'fixture-manifest.json')
if a.capture_buffers:
    for test in manifest:test['env']['SF_VT_CAPTURE_BUFFERS']=str(a.capture_buffers)
for test in manifest:
    if a.hold_final:test['env']['SF_CAPTURE_HOLD_FINAL']='1'
    if a.retire_pool:test['env']['SF_VT_RETIRE_POOL_AT_INVALIDATE']='1'
    if a.gpu_final:
        test['env'].update(SF_CAPTURE_GPU_FINAL='1',
            SF_CAPTURE_EXPECT_GL=str(a.graphics_override/'gl/Frameworks/OpenGL.framework/Versions/A/OpenGL'),
            DYLD_FRAMEWORK_PATH='@ROOT@/Frameworks:'+str(a.graphics_override/'gl/Frameworks'),
            DYLD_LIBRARY_PATH=str(a.graphics_override/'gl/lib'))
(a.stage/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(a.stage/'scope.json').write_text(json.dumps(dict(
    scope='STRICT real GPU storage/import, independent visible-pixel hashes/PTS, retained-picture64. Not GPU sampling/completion/browser performance.',
    surface=str(a.surface.resolve()),providers=str(a.providers.resolve()),probe=str(a.probe.resolve()),
    required_mode='require',fallback_is_failure=True,
    hold_final=a.hold_final,retire_pool=a.retire_pool,gpu_final=a.gpu_final,
    graphics_override=str(a.graphics_override) if a.graphics_override else None),indent=2)+'\n')
rows=[dict(path=str(f.relative_to(a.stage)),sha256=hashlib.sha256(f.read_bytes()).hexdigest())
      for f in sorted(a.stage.rglob('*')) if f.is_file() and not f.is_symlink()]
(a.stage/'cold-files.json').write_text(json.dumps(rows,indent=2)+'\n')
print(json.dumps(dict(stage=str(a.stage),files=len(rows))))
