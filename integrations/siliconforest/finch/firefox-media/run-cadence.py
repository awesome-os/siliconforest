#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Host collector. Caller holds gui.lock -> bench; fresh coordination gate.

control cold-copies a fresh packet; browser uses its recorded cold receipt.
No system installs, owner profile, input or process control outside our family.
"""
import argparse
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import shlex
import subprocess
import tarfile

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('stage',type=Path);p.add_argument('phase',choices=['control','browser','profile','rates','copy','shared','codecs','youtube','youtube-profile','youtube1080','youtubehw','youtube-policy','av1-policy','frame-id','mse','mse-installed','mse-scroll'])
p.add_argument('--reuse-media-profile',type=Path)
p.add_argument('--start-media',type=float)
p.add_argument('--compare-pixels',action='store_true')
p.add_argument('--graphics-override',help='FX-qualified complete graphics cohort under the runtime overrides directory')
p.add_argument('--frame-id-geometry',help='Explicit private-CGL marker geometry; actual frame0 must qualify it')
p.add_argument('--frame-id-cgl',action='store_true',help='Only the packaged private CGL; retains installed media/GLAPI/surface/broker/UI')
p.add_argument('--qc-copy',choices=['off','on'],help='Private frozen QC paired-copy browser comparison')
p.add_argument('--qc-1080-only',action='store_true')
p.add_argument('--qc-settle',type=int,choices=[0,60],default=0)
p.add_argument('--frame-id-dimensions-only',action='store_true',help='Private CGL paused dimensions preflight, no geometry/readback/playback')
p.add_argument('--frame-id-profile',action='store_true',help='Gecko media/renderer stacks for the owned frame-ID diagnostic; never speed qualification')
p.add_argument('--frame-id-quiet-rollouts',action='store_true',help='Owned test-profile Nimbus rollout/study control; no installed preference change')
p.add_argument('--no-native-compositor',action='store_true',help='Explicit own-profile tile-path diagnostic')
p.add_argument('--timer-control',action='store_true',help='Explicit own-process PD CV timer comparison')
p.add_argument('--lifetime-control',action='store_true',help='Owned strict playback end/unload observation')
p.add_argument('--retire-pool',action='store_true',help='Private retirement comparison, requires lifetime control')
p.add_argument('--trace-media',action='store_true',help='Diagnostic CV/VT/CGL boundary tracing; never performance qualification')
p.add_argument('--view-state',action='store_true',help='MSE-only read-only native view observation; excludes performance qualification')
a=p.parse_args();repo=Path(__file__).resolve().parents[4]
if a.qc_copy and a.phase!='browser':raise ValueError('QC caller requires its separate browser phase')
if (a.qc_1080_only or a.qc_settle) and not a.qc_copy:raise ValueError('Private QC control required')
if a.frame_id_geometry:
    if a.phase!='frame-id' or not a.frame_id_cgl:raise ValueError('Marker geometry requires its private CGL frame-ID case')
    from frame_geometry import parse
    parse(a.frame_id_geometry)
if a.frame_id_cgl and (a.phase!='frame-id' or a.graphics_override or not (a.frame_id_geometry or a.frame_id_dimensions_only)):
    raise ValueError('Private frame-ID CGL requires explicit geometry and no graphics/media cohort override')
if a.frame_id_dimensions_only and (not a.frame_id_cgl or a.frame_id_geometry):
    raise ValueError('Dimensions-only requires private CGL and no geometry')
if a.frame_id_profile and (a.phase!='frame-id' or not a.frame_id_cgl or a.frame_id_dimensions_only):
    raise ValueError('Frame-ID profiling requires its private timed marker case')
if a.frame_id_quiet_rollouts and (a.phase!='frame-id' or a.frame_id_dimensions_only):
    raise ValueError('Rollout control requires the owned timed frame-ID case')
if (a.reuse_media_profile or a.start_media is not None or a.compare_pixels) and a.phase not in ['youtube1080','youtubehw']:
    raise ValueError('Own profile/seek/pixel controls require their1080 streaming case')
if a.view_state and a.phase!='mse':raise ValueError('View-state requires separate MSE phase')
if (a.lifetime_control or a.retire_pool) and a.phase!='shared':raise ValueError('Separate strict shared case required')
if a.retire_pool and not a.lifetime_control:raise ValueError('Retirement requires lifetime control')
if a.trace_media and a.phase!='shared':raise ValueError('Boundary trace requires its separate strict shared case')
if not os.environ.get('BENCH_OWNER'):raise ValueError('Caller must hold gui.lock then bench')
if not a.stage.name.startswith('firefox-media-cadence-packet-'):raise ValueError('Own cadence packet required')
dest='/private/var/db/sffx/'+a.stage.name
def command(argv,**kwargs):return subprocess.run(argv,check=True,**kwargs)
command(['python3',str(repo/'integrations/siliconforest/finch/tools/coord-gate.py'),
         '--max-seconds','170'],cwd=repo)
out=a.stage/('target-'+a.phase)
out.mkdir()
ssh=['ssh','-o','BatchMode=yes','-o','StrictHostKeyChecking=yes','-o','ConnectTimeout=8',
     '-o','UserKnownHostsFile='+str(Path.home()/'.ssh/known_hosts_m1_siliconforest'),
     '-o','HostKeyAlias=m1-siliconforest','root@10.1.1.211']
receipt=a.stage/'target-cold-verified.json'
result=None
collection_error=None
def log(message):
    with (repo/'runs/m1-coord/log.txt').open('a') as f:
        f.write(datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')+' CODEX '+message+'\n')
log('media cadence '+a.phase+' START; own CLI/browser profile only <=170s, no input/install/ownerprocess signals; no owneridlewait.')
try:
    if a.phase=='control':
        # Do not include this collector's growing output directory in the
        # transferred packet. put.py takes a fixed source snapshot first.
        import tempfile,shutil
        with tempfile.TemporaryDirectory() as temporary:
            source=Path(temporary)/'packet'
            shutil.copytree(a.stage,source,ignore=shutil.ignore_patterns('target-*'))
            command(['python3',str(repo/'integrations/siliconforest/firefox/put.py'),
                     '--source',str(source),'--destination',dest,'--receipt',str(receipt)],cwd=repo)
        argv=['python3',dest+'/cadence-control.py'];limit=100
    else:
        cold=json.loads(receipt.read_text())
        if cold.get('destination')!=dest or cold.get('cold_before_execution') is not True:
            raise ValueError('Missing prior cold receipt')
        argv=['python3',dest+'/cadence-browser.py','--run',dest+'/'+a.phase+'-run']
        if a.phase=='profile':argv+=['--profile-video']
        if a.qc_copy:argv+=['--qc-copy',a.qc_copy,'--installed-media','--frame-mode','require','--without-trace']
        if a.qc_1080_only:argv+=['--qc-1080-only']
        if a.qc_settle:argv+=['--qc-settle',str(a.qc_settle)]
        if a.phase=='rates':argv+=['--rate-control']
        if a.phase=='codecs':argv+=['--codec-matrix','--without-trace']
        if a.phase=='mse':argv+=['--mse','--staged-media','--frame-mode','require','--without-trace']
        if a.phase in ['mse-installed','mse-scroll']:
            argv+=['--mse','--installed-media','--frame-mode','require','--without-trace']
            if a.phase=='mse-scroll':argv+=['--mse-scroll']
        if a.view_state:argv+=['--view-state']
        if a.phase in ['youtube','youtube-profile','youtube1080','youtubehw','youtube-policy']:
            argv+=['--youtube','--without-trace']
            if a.phase=='youtube-profile':argv+=['--youtube-profile']
            if a.phase in ['youtube1080','youtubehw','youtube-policy']:argv+=['--youtube-1080']
            if a.phase=='youtubehw':argv+=['--disable-av1']
            if a.phase=='youtube-policy':argv+=['--installed-codec-policy']
        if a.phase=='av1-policy':argv+=['--installed-codec-policy','--installed-media','--codec-matrix','--av1-switchback','--without-trace']
        if a.phase=='frame-id':argv+=['--frame-id','--installed-media','--frame-mode','require','--without-trace']
        if a.reuse_media_profile:argv+=['--reuse-media-profile',str(a.reuse_media_profile)]
        if a.start_media is not None:argv+=['--start-media',str(a.start_media)]
        if a.compare_pixels:argv+=['--compare-pixels']
        if a.phase in ['copy','shared']:
            argv+=['--staged-media','--frame-mode','require' if a.phase=='shared' else 'copy']
            if not a.trace_media:argv+=['--without-trace']
        if a.graphics_override:
            if a.phase not in ['copy','shared']:raise ValueError('Graphics comparisons require staged-media copy/shared phase')
            argv+=['--graphics-override',a.graphics_override]
        if a.frame_id_geometry:argv+=['--frame-id-geometry',a.frame_id_geometry]
        if a.frame_id_cgl:argv+=['--frame-id-cgl']
        if a.frame_id_dimensions_only:argv+=['--frame-id-dimensions-only']
        if a.frame_id_profile:argv+=['--profile-video']
        if a.frame_id_quiet_rollouts:argv+=['--frame-id-quiet-rollouts']
        if a.no_native_compositor:argv+=['--no-native-compositor']
        if a.timer_control:argv+=['--timer-control']
        if a.lifetime_control:argv+=['--lifetime-control']
        if a.retire_pool:argv+=['--retire-pool']
        limit=165
    with (out/'launcher.log').open('w') as stream:
        result=subprocess.run(ssh+[shlex.join(argv)],stdout=stream,stderr=subprocess.STDOUT,timeout=limit)
    (out/'runner-status.json').write_text(json.dumps(dict(returncode=result.returncode,
        diagnostic_only=True,synthetic_input=False,owner_idle_wait=False))+'\n')
finally:
    names=['cold-verified.json','control-results.json'] if a.phase=='control' else [a.phase+'-run']
    archive=out/'evidence.tar.gz'
    r=None
    try:
        with archive.open('wb') as stream:
            r=subprocess.run(ssh+[shlex.join(['tar','-C',dest,'--exclude=*/profile','-czf','-']+names)],
                             stdout=stream,stderr=subprocess.PIPE,timeout=30)
    except subprocess.TimeoutExpired as error:
        collection_error=str(error)
    (out/'collection.stderr').write_bytes(r.stderr if r else b'Collection timed out; partial archive retained\n')
    (out/'collection-status.json').write_text(json.dumps(dict(
        returncode=r.returncode if r else None,error=collection_error,
        excluded=['*/profile'],profile_retained_on_target=True,
        scope='Results, logs, captures and diagnostic profiles outside the Firefox user profile; user profile/minidumps retained on target'))+'\n')
    if r is not None and r.returncode==0:
        with tarfile.open(archive) as t:
            members=t.getmembers()
            (out/'evidence-links.json').write_text(json.dumps([dict(name=m.name,target=m.linkname)
                for m in members if m.issym() or m.islnk()])+'\n')
            t.extractall(out,members=[m for m in members if not m.issym() and not m.islnk()],filter='data')
    log('media cadence '+a.phase+' END; collection '+('OK' if r is not None and r.returncode==0 else 'FAILED')+', no installed changes/input.')
if collection_error:raise RuntimeError(collection_error)
if a.lifetime_control and result.returncode==0 and r.returncode==0:
    browser=json.loads((out/(a.phase+'-run')/'browser/result.json').read_text())
    clips=browser.get('video_measurements',[])
    checks={'two_owned_clips':len(clips)==2,'browser_survived':browser.get('browser_survived_run') is True}
    for index,clip in enumerate(clips):
        video=clip.get('video',{});control=video.get('lifetime_control',{})
        checks[str(index)+'_lifetime_observed']=bool(control) and not video.get('lifetime_control_error')
        checks[str(index)+'_source_unloaded']=control.get('unload',{}).get('paused') is True
        checks[str(index)+'_mode_matches']=control.get('retire_pool') is a.retire_pool
    admission=dict(status='PASS' if all(checks.values()) else 'FAIL',checks=checks,
        scope='Owned end/unload sequence only; memory savings, timing and visible playback require separate review')
    (out/'lifetime-admission.json').write_text(json.dumps(admission,indent=2)+'\n')
    if admission['status']!='PASS':raise RuntimeError('Owned browser lifetime sequence incomplete; raw failure retained')
raise SystemExit(result.returncode or r.returncode)
