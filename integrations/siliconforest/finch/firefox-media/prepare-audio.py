#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Create a fresh reproducible query/silent-lifecycle prefix from Linux output."""
import argparse,hashlib,json,shutil,subprocess
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--package',type=Path,required=True);p.add_argument('--stage',type=Path,required=True)
g=p.add_mutually_exclusive_group()
g.add_argument('--latency',action='store_true',help='Only the own silent timestamp boundary probe')
g.add_argument('--admission',action='store_true',help='Only public VT session admission; no samples/output')
p.add_argument('--fixture',type=Path,help='Our generated HEVC Main10 PQ fixture; required for --admission')
p.add_argument('--clock-provider',type=Path,help='Private AU A/B provider; requires --latency, never stages CoreAudio')
p.add_argument('--clock-family',type=Path,help='Full Finch AudioToolbox family with normalized retained PD child; requires --latency')
p.add_argument('--family-tests',type=Path,help='Linux-built installed-family codec/queue programs; requires --clock-family and --fixtures')
p.add_argument('--fixtures',type=Path,help='Our generated audio codec fixtures')
p.add_argument('--lifecycle-package',type=Path,help='Own silent AU lifecycle program and observer, only with --clock-family')
a=p.parse_args();here=Path(__file__).resolve().parent;tools=here.parent/'tools'
if a.stage.exists():raise ValueError('Fresh stage required')
if a.clock_provider and not a.latency:raise ValueError('--clock-provider requires --latency')
if a.clock_family and (not a.latency or a.clock_provider):raise ValueError('--clock-family requires --latency and excludes --clock-provider')
if bool(a.family_tests)!=bool(a.fixtures) or (a.family_tests and not a.clock_family):raise ValueError('Family tests require --clock-family and --fixtures together')
if a.lifecycle_package and not a.clock_family:raise ValueError('--lifecycle-package requires --clock-family')
if bool(a.fixture)!=a.admission:raise ValueError('--fixture required exactly with --admission')
a.stage.mkdir()
for directory in ['bin','lib']:
    if (a.package/directory).is_dir():shutil.copytree(a.package/directory,a.stage/directory)
    elif directory=='lib' and a.admission:(a.stage/directory).mkdir()
    else:raise ValueError('Missing package directory: '+directory)
    (a.stage/directory).chmod(0o755)
    subprocess.run(['python3',str(tools/'portable-tests.py'),str(here.parents[1]/'homebrew/hb_macho.py'),str(a.stage/directory),str(a.stage/('portable-'+directory+'.json'))],check=True)
for source,destination in [(here/'owned-cli.py','owned-cli.py'),(here/'cold-capture.py','compare-tests.py'),(tools/'owner_input.py','owner_input.py'),(tools/'compare-tests.py','compare-tests-engine.py')]:
    shutil.copy2(source,a.stage/destination)
if a.admission:
    (a.stage/'media').mkdir();shutil.copy2(a.fixture,a.stage/'media/hevc-main10-pq.mp4')
manifest=[dict(name='firefox-media-'+mode,argv=['@PYTHON@','owned-cli.py',mode],mode='exit',skip_lines=0,timeout=18,
               env={'SF_FIREFOX_MEDIA_TRACE':'1','SF_FIREFOX_MEDIA_TRACE_SECONDS':'10'}) for mode in (['codec-nv12-hw','codec-p010-hw','codec-p010-sw'] if a.admission else ['latency'] if a.latency else ['query','silence'])]
if a.clock_provider:
    import runpy
    api=runpy.run_path(str(here.parents[1]/'homebrew/hb_macho.py'))
    for name in ['AudioUnit','AudioToolbox']:
        dest=a.stage/'Frameworks'/(name+'.framework')
        shutil.copytree(a.clock_provider/'System/Library/Frameworks'/(name+'.framework'),dest,symlinks=True)
        image=dest/'Versions/A'/name;image.chmod(0o755)
        image.write_bytes(api['resign'](image.read_bytes())[0])
        if api['verify'](image.read_bytes()):raise ValueError('Private provider signature failed')
    manifest=[]
    for mode,enabled in [('latency','0'),('latency-normalized','1')]:
        manifest.append(dict(name='firefox-audio-clock-'+mode,argv=['@PYTHON@','owned-cli.py',mode],mode='exit',skip_lines=0,timeout=18,
            env={'DYLD_FRAMEWORK_PATH':'@ROOT@/Frameworks','SF_AU_NORMALIZE_HAL_CLOCK':enabled}))
if a.clock_family:
    # Keep canonical CoreAudio and AudioUnit. Its forwarding imports must select
    # this same full AudioToolbox, not a second competing thin PD provider.
    shutil.copytree(a.clock_family/'Frameworks/AudioToolbox.framework',
                    a.stage/'Frameworks/AudioToolbox.framework',symlinks=True)
    manifest=[]
    env={'DYLD_FRAMEWORK_PATH':'@ROOT@/Frameworks','SF_AU_NORMALIZE_HAL_CLOCK':'1'}
    for mode,enabled in [('latency','0'),('latency-normalized','1')]:
        manifest.append(dict(name='firefox-full-audio-clock-'+mode,
            argv=['@PYTHON@','owned-cli.py',mode],mode='exit',skip_lines=0,timeout=18,
            env=dict(env,SF_AU_NORMALIZE_HAL_CLOCK=enabled)))
    if a.family_tests:
        for name in ['audio-file-probe','audioconverter-test','extaudiofile-test','audio-decode-probe','browser-audio-probe','sf-afplay']:
            shutil.copy2(a.family_tests/'bin'/name,a.stage/'bin'/name)
        subprocess.run(['python3',str(tools/'portable-tests.py'),str(here.parents[1]/'homebrew/hb_macho.py'),str(a.stage/'bin'),str(a.stage/'portable-family-bin.json')],check=True)
        (a.stage/'fixtures').mkdir()
        names=['tone.wav','tone-aac.m4a','tone-alac.m4a','tone.mp3']
        for name in names:shutil.copy2(a.fixtures/name,a.stage/'fixtures'/name)
        for name in ['audio-file-probe','audioconverter-test','extaudiofile-test']:
            manifest.append(dict(name='normalized-'+name,argv=['bin/'+name],mode='exit',skip_lines=0,timeout=12,env=env))
        manifest.append(dict(name='normalized-browser-aac',argv=['bin/browser-audio-probe','fixtures/tone-aac.m4a'],mode='exit',skip_lines=0,timeout=12,env=env))
        for name in names:
            manifest.append(dict(name='normalized-decode-'+name,
                argv=['bin/audio-decode-probe','fixtures/'+name],mode='exit',skip_lines=0,timeout=12,env=env))
        for name in names[:2]:
            manifest.append(dict(name='normalized-queue-'+name,
                argv=['bin/sf-afplay','fixtures/'+name],mode='exit',skip_lines=0,timeout=12,env=env))
    if a.lifecycle_package:
        shutil.copy2(a.lifecycle_package/'bin/audio-media-probe',a.stage/'bin/audio-media-probe')
        shutil.copy2(a.lifecycle_package/'lib/SFFirefoxMediaObserver.dylib',a.stage/'lib/SFFirefoxMediaObserver.dylib')
        for directory in ['bin','lib']:
            subprocess.run(['python3',str(tools/'portable-tests.py'),str(here.parents[1]/'homebrew/hb_macho.py'),str(a.stage/directory),str(a.stage/('portable-lifecycle-'+directory+'.json'))],check=True)
        for mode in ['query','silence']:
            manifest.append(dict(name='normalized-'+mode,argv=['@PYTHON@','owned-cli.py',mode],mode='exit',skip_lines=0,timeout=18,
                env=dict(env,SF_FIREFOX_MEDIA_TRACE='1',SF_FIREFOX_MEDIA_TRACE_SECONDS='10')))
(a.stage/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
rows=[dict(path=str(f.relative_to(a.stage)),sha256=hashlib.sha256(f.read_bytes()).hexdigest()) for f in sorted(a.stage.rglob('*')) if f.is_file() and f.name!='cold-files.json']
(a.stage/'cold-files.json').write_text(json.dumps(rows,indent=2)+'\n')
print(json.dumps(dict(stage=str(a.stage),files=len(rows),cold_manifest_sha256=hashlib.sha256((a.stage/'cold-files.json').read_bytes()).hexdigest())))
