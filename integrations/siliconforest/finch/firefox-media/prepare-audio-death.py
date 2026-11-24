#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Freeze Linux-built observers and our no-input client-death diagnostic."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--hal-observer',type=Path,required=True)
p.add_argument('--loss-observer',type=Path,required=True)
p.add_argument('--stage',type=Path,required=True)
p.add_argument('--clock-family',type=Path,help='Optional complete normalized AudioToolbox family for a prefix-only comparison')
p.add_argument('--aac-fixture',type=Path,help='Our generated AAC .m4a instead of the generated WAV document')
p.add_argument('--pcm-observer',type=Path,help='Optional bounded native output meter for own state controls')
p.add_argument('--session-watch',type=Path,help='Linux-built read-only exact client-slot observer')
p.add_argument('--audio-server',type=Path,help='Private exact original installed daemon UUID/table-offset/source-hash specification')
a=p.parse_args();here=Path(__file__).resolve().parent
if bool(a.session_watch)!=bool(a.audio_server):raise ValueError('Exact daemon specification and observer must be paired')
if a.stage.exists():raise ValueError('Fresh packet required')
a.stage.mkdir();(a.stage/'lib').mkdir();(a.stage/'tools').mkdir()
for source,name in [(a.hal_observer,'SFFirefoxMediaObserver.dylib'),(a.loss_observer,'SFFirefoxWaylandLoss.dylib')]:
    shutil.copy2(source,a.stage/'lib'/name)
if a.pcm_observer:shutil.copy2(a.pcm_observer,a.stage/'lib/SFHALPCMTrace.dylib')
if a.session_watch:
    (a.stage/'bin').mkdir();shutil.copy2(a.session_watch,a.stage/'bin/audio-session-watch')
    shutil.copy2(a.audio_server,a.stage/'audio-server.json')
subprocess.run(['python3',str(here.parent/'tools/portable-tests.py'),
    str(here.parents[1]/'homebrew/hb_macho.py'),str(a.stage/'lib'),str(a.stage/'portable-observers.json')],check=True)
shutil.copy2(here/'audio-death-browser.py',a.stage/'audio-death-browser.py')
shutil.copy2(here/'audio_state_controls.py',a.stage/'audio_state_controls.py')
shutil.copy2(here/'audio_logs.py',a.stage/'audio_logs.py')
shutil.copy2(here/'audio_session_watch.py',a.stage/'audio_session_watch.py')
shutil.copy2(here/'media_images.py',a.stage/'media_images.py')
for file in sorted((here.parents[1]/'firefox').iterdir()):
    if file.is_file() and file.suffix in ['.py','.html','.js','.json']:
        shutil.copy2(file,a.stage/'tools'/file.name)
if a.clock_family:
    shutil.copytree(a.clock_family/'Frameworks/AudioToolbox.framework',a.stage/'Frameworks/AudioToolbox.framework',symlinks=True)
    shutil.copy2(here/'media-launch.py',a.stage/'media-launch.py')
if a.aac_fixture:
    (a.stage/'media').mkdir();shutil.copy2(a.aac_fixture,a.stage/'media/tone-aac.m4a')
    tool=a.stage/'tools/lifecycle_observer.py';text=tool.read_text()
    anchor='tone = stream.getvalue()'
    if text.count(anchor)!=1 or text.count("tone, 'audio/wav'")!=1:raise ValueError('Own audio fixture source changed')
    text=text.replace(anchor,"tone = (Path(__file__).parents[1]/'media/tone-aac.m4a').read_bytes()")
    text=text.replace("tone, 'audio/wav'","tone, 'audio/mp4'").replace('tone.wav','tone.m4a');tool.write_text(text)
    page=a.stage/'tools/lifecycle-page.html';page.write_text(page.read_text().replace('tone.wav','tone.m4a'))
rows={str(f.relative_to(a.stage)):hashlib.sha256(f.read_bytes()).hexdigest()
      for f in sorted(a.stage.rglob('*')) if f.is_file()}
(a.stage/'packet-files.json').write_text(json.dumps(rows,indent=2)+'\n')
print(json.dumps(dict(stage=str(a.stage),files=len(rows),observers={k:v for k,v in rows.items() if k.startswith('lib/')})))
