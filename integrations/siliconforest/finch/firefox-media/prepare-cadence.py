#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Prepare our Linux-built observer, public controls and no-input browser runner."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--probes',type=Path,required=True)
p.add_argument('--admission',type=Path,required=True)
p.add_argument('--fixture',type=Path,required=True)
p.add_argument('--frame-id-fixture',type=Path,help='Own Linux-verified900-frame Gray-code movie and manifest')
p.add_argument('--frame-id-cgl',type=Path,help='Linux-built CGL hook prefix; package ONLY OpenGL.framework')
p.add_argument('--frame-id-buffered',action='store_true',help='Private RAM observer needs an explicit post-measurement flush')
p.add_argument('--qc-nv12',type=Path,help='Original private frozen QuartzCore caller build; paired with --qc-cgl')
p.add_argument('--qc-cgl',type=Path,help='Current qualified CGL GPU-copy build; private comparison only')
p.add_argument('--stage',type=Path,required=True)
p.add_argument('--providers',type=Path,help='Reviewed media payload only; graphics and broker remain selected by FX')
p.add_argument('--gpu-counter-spec',type=Path,help='Read-only CGL counters: exact runtime provider path/UUID and Linux symbol offset')
p.add_argument('--policy-probe',type=Path,help='Linux-built read-only Mach helper, required with --gpu-counter-spec')
p.add_argument('--codec-fixtures',type=Path,help='Our generated codec matrix with its Linux hash/probe manifest')
p.add_argument('--codec-profile',choices=['basic','highrate'],default='basic')
p.add_argument('--mse-fixtures',type=Path,help='Own Linux-generated representation-switch files and manifest')
p.add_argument('--mse-pcm-trace',type=Path,help='Linux-built output aggregate meter; required for MSE')
p.add_argument('--mse-hal-trace',type=Path,help='Linux-built HAL ring observer; required for MSE')
p.add_argument('--view-state',type=Path,help='Linux-built read-only AppKit getter observer/control prefix; MSE diagnostic only')
a=p.parse_args();here=Path(__file__).resolve().parent
if bool(a.qc_nv12)!=bool(a.qc_cgl):raise ValueError('Private QC and CGL builds required together')
if a.qc_nv12 and (a.providers or a.frame_id_cgl or a.frame_id_fixture or a.mse_fixtures or a.codec_fixtures or not a.gpu_counter_spec):
    raise ValueError('Separate private QC comparison requires its exact GPU counter specification')
if a.view_state and not a.mse_fixtures:raise ValueError('View-state diagnostic requires its own MSE case')
if bool(a.mse_fixtures)!=bool(a.mse_pcm_trace) or bool(a.mse_fixtures)!=bool(a.mse_hal_trace):
    raise ValueError('MSE requires its generated fixtures and both qualified native observers')
if bool(a.gpu_counter_spec)!=bool(a.policy_probe):raise ValueError('Counter specification and read-only helper required together')
if a.frame_id_cgl and (not a.frame_id_fixture or a.providers):
    raise ValueError('Private frame-ID CGL must retain installed media and use the own fixture')
if a.frame_id_buffered and not a.frame_id_cgl:raise ValueError('Buffered observer requires its private CGL')
if a.stage.exists():raise ValueError('Fresh destination required')
a.stage.mkdir();(a.stage/'bin').mkdir();(a.stage/'lib').mkdir();(a.stage/'media').mkdir()
for name in ['cadence-baseline','cadence-observed']:
    shutil.copy2(a.probes/'bin'/name,a.stage/'bin'/name)
shutil.copy2(a.probes/'lib/SFMediaCadenceTrace.dylib',a.stage/'lib/SFMediaCadenceTrace.dylib')
if a.mse_fixtures:
    manifest=json.loads((a.mse_fixtures/'manifest.json').read_text())
    target=a.stage/'media/mse';target.mkdir()
    for row in manifest['segments']:
        name=row['file']
        if Path(name).name!=name or not name.endswith('.mp4'):raise ValueError('Own MSE basename required')
        source=a.mse_fixtures/name;data=source.read_bytes()
        if len(data)!=row['bytes'] or hashlib.sha256(data).hexdigest()!=row['sha256']:
            raise ValueError('Generated MSE input changed')
        shutil.copy2(source,target/name)
    shutil.copy2(a.mse_fixtures/'manifest.json',target/'manifest.json')
    shutil.copy2(here/'mse-page.html',target/'index.html')
    shutil.copy2(a.mse_pcm_trace,a.stage/'lib/SFHALPCMTrace.dylib')
    shutil.copy2(a.mse_hal_trace,a.stage/'lib/SFFirefoxMediaObserver.dylib')
if a.view_state:
    shutil.copy2(a.view_state/'lib/SFFirefoxViewState.dylib',a.stage/'lib/SFFirefoxViewState.dylib')
    shutil.copy2(a.view_state/'bin/view-state-control',a.stage/'bin/view-state-control')
shutil.copy2(a.admission/'bin/vt-codec-admission',a.stage/'bin/vt-codec-admission')
shutil.copy2(a.fixture,a.stage/'media/hevc-main10-pq.mp4')
if a.frame_id_fixture:
    folder=a.frame_id_fixture;frozen=json.loads((folder/'manifest.json').read_text())
    name='frame-id-1080p60.mp4';source=folder/name
    if frozen.get('file')!=name or hashlib.sha256(source.read_bytes()).hexdigest()!=frozen.get('sha256'):
        raise ValueError('Frame identity fixture changed')
    target=a.stage/'media/frame-id';target.mkdir()
    shutil.copy2(source,target/name);shutil.copy2(folder/'manifest.json',target/'manifest.json')
    shutil.copy2(here/'frame-id-page.html',target/'index.html')
if a.frame_id_cgl:
    if not str(a.frame_id_cgl.resolve()).startswith('/nix/store/'):raise ValueError('Original Linux Nix CGL output required')
    target=a.stage/'frame-id-cgl/Frameworks';target.mkdir(parents=True)
    shutil.copytree(a.frame_id_cgl/'Frameworks/OpenGL.framework',target/'OpenGL.framework',symlinks=True)
    import struct
    data=(a.frame_id_cgl/'Frameworks/OpenGL.framework/Versions/A/OpenGL').read_bytes();uuids=[];offset=32
    if struct.unpack_from('<I',data)[0]!=0xfeedfacf:raise ValueError('Thin original arm64 CGL required')
    for _ in range(struct.unpack_from('<I',data,16)[0]):
        command,size=struct.unpack_from('<II',data,offset)
        if size<8 or offset+size>len(data):raise ValueError('Invalid original CGL load command')
        if command==27:uuids.append(data[offset+8:offset+24].hex())
        offset+=size
    if len(uuids)!=1:raise ValueError('One original CGL UUID required')
    (a.stage/'frame-id-cgl/identity.json').write_text(json.dumps(dict(original_linux_nix=str(a.frame_id_cgl),
        sha256=hashlib.sha256(data).hexdigest(),uuid=uuids[0],
        relative_binary='frame-id-cgl/Frameworks/OpenGL.framework/Versions/A/OpenGL',
        explicit_log_flush=a.frame_id_buffered,
        scope='Private CGL only; canonical GLAPI/IOSurface/broker/UI and selected media remain'),indent=2)+'\n')
if a.qc_nv12:
    from qc_copy_control import binary_identity
    qc=a.qc_nv12/'System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore'
    cgl=a.qc_cgl/'Frameworks/OpenGL.framework/Versions/A/OpenGL'
    identity=dict(quartzcore=binary_identity(qc,'_sfca_video_upload_stats'),
                  cgl=binary_identity(cgl,'_sf_gpu_stats'),installed=False)
    target=a.stage/'qc-nv12';(target/'lib').mkdir(parents=True)
    shutil.copy2(qc,target/'lib/QuartzCore.pd')
    (target/'Frameworks').mkdir()
    shutil.copytree(a.qc_cgl/'Frameworks/OpenGL.framework',target/'Frameworks/OpenGL.framework',symlinks=True)
    (target/'identity.json').write_text(json.dumps(identity,indent=2)+'\n')
if a.codec_fixtures:
    manifest=json.loads((a.codec_fixtures/'manifest.json').read_text())
    rows={row['file']:row for row in manifest['fixtures']}
    names=['av1-1080p30.webm','vp9-1080p60.webm','hevc-main10-pq.mp4','vp9-profile2-pq.webm']
    if a.codec_profile=='highrate':names=['av1-1080p60.webm','vp9-4k30.webm']
    target=a.stage/'media/codecs';target.mkdir()
    for name in names:
        source=a.codec_fixtures/name
        if hashlib.sha256(source.read_bytes()).hexdigest()!=rows[name]['sha256']:
            raise ValueError('Generated codec fixture hash changed: '+name)
        shutil.copy2(source,target/name)
    shutil.copy2(a.codec_fixtures/'manifest.json',target/'manifest.json')
    (target/'selection.json').write_text(json.dumps(names)+'\n')
    shutil.copy2(here/'codec-capabilities.js',a.stage/'codec-capabilities.js')
if a.policy_probe:shutil.copy2(a.policy_probe,a.stage/'bin/thread-policy-probe')
for directory in ['bin','lib']:
    subprocess.run(['python3',str(here.parent/'tools/portable-tests.py'),
        str(here.parents[1]/'homebrew/hb_macho.py'),str(a.stage/directory),
        str(a.stage/('portable-'+directory+'.json'))],check=True)
for name in ['cadence-browser.py','cadence-control.py','cadence-rate.js','media-capture.js','process_cpu.py','media-launch.py','media_images.py','video_selection.py','audio_logs.py','youtube_quality.py','media_profile_admission.py','codec_policy_guard.py','frame_geometry.py','frame_marker.py','frame_probe_admission.py','qc_copy_control.py']:
    shutil.copy2(here/name,a.stage/name)
shutil.copy2(here.parent/'tools/owner_input.py',a.stage/'owner_input.py')
tools=here.parents[1]/'firefox';(a.stage/'tools').mkdir()
for file in sorted(tools.iterdir()):
    if file.is_file() and file.suffix in ['.py','.html','.js','.json']:
        shutil.copy2(file,a.stage/'tools'/file.name)
page=a.stage/'tools/video-page.html'
page.write_text(page.read_text()+'\n<script src="../cadence-rate.js"></script>\n<script src="../media-capture.js"></script>\n')
if a.gpu_counter_spec:shutil.copy2(a.gpu_counter_spec,a.stage/'tools/gpu-counters.json')
if a.providers:
    frameworks=a.providers/'Frameworks'
    names={p.name for p in frameworks.iterdir()}
    if not names or names-{'CoreVideo.framework','VideoToolbox.framework','AudioToolbox.framework'}:
        raise ValueError('Only reviewed media frameworks may be packaged')
    shutil.copytree(frameworks,a.stage/'Frameworks',symlinks=True)
rows={str(f.relative_to(a.stage)):hashlib.sha256(f.read_bytes()).hexdigest()
      for f in sorted(a.stage.rglob('*')) if f.is_file()}
(a.stage/'packet-files.json').write_text(json.dumps(rows,indent=2)+'\n')
print(json.dumps(dict(stage=str(a.stage),files=len(rows),trace_sha256=rows['lib/SFMediaCadenceTrace.dylib'])))
