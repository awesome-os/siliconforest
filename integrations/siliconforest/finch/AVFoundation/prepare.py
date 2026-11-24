#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Prepare new isolated asset stages; reference packets contain our binaries only."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for n in ['framework','tests','media-stage','fixtures','stage','reference-packet']:
        ap.add_argument('--'+n,type=Path,required=True)
    ap.add_argument('--ut-provider',type=Path,help='Optional retained PD UniformTypeIdentifiers package, staged only')
    a=ap.parse_args();here=Path(__file__).resolve().parent
    if a.stage.exists() or a.reference_packet.exists():raise ValueError('Destinations must be new')
    a.stage.mkdir(parents=True);fw=a.stage/'System/Library/Frameworks';fw.mkdir(parents=True)
    for n in ['CoreMedia','CoreVideo','AudioToolbox','AVFAudio']:
        shutil.copytree(a.media_stage/'System/Library/Frameworks'/(n+'.framework'),fw/(n+'.framework'),symlinks=True)
    if a.ut_provider:
        shutil.copytree(a.ut_provider/'System/Library/Frameworks/UniformTypeIdentifiers.framework',fw/'UniformTypeIdentifiers.framework',symlinks=True)
    shutil.copytree(a.framework/'System/Library/Frameworks/AVFoundation.framework',fw/'AVFoundation.framework',symlinks=True)
    shutil.copytree(a.tests/'bin',a.stage/'bin');(a.stage/'bin').chmod(0o755)
    subprocess.run([sys.executable,str(here.parent/'tools/portable-tests.py'),str(here.parent.parent/'homebrew/hb_macho.py'),str(a.stage/'bin'),str(a.stage/'binary-hashes.json')],check=True)
    (a.stage/'fixtures').mkdir()
    for ext in ['mp4','mov']:shutil.copy2(a.fixtures/('test-av.'+ext),a.stage/'fixtures')
    rows=[dict(name='avasset-'+ext,argv=['bin/avasset-probe','fixtures/test-av.'+ext,'mux.'+ext],mode='reference',skip_lines=0,timeout=30,env={'DYLD_FRAMEWORK_PATH':'@ROOT@/System/Library/Frameworks'}) for ext in ['mp4','mov']]
    (a.stage/'manifest.json').write_text(json.dumps(rows,indent=2)+'\n');shutil.copy2(here.parent/'tools/compare-tests.py',a.stage)
    p=a.reference_packet;p.mkdir(parents=True)
    for n in ['bin','fixtures']:
        shutil.copytree(a.stage/n,p/n);(p/n).chmod(0o755)
    refs=[{k:v for k,v in r.items() if k!='env'} for r in rows]
    (p/'manifest.json').write_text(json.dumps(refs,indent=2)+'\n');shutil.copy2(here.parent/'tools/compare-tests.py',p)
    (p/'SHA256SUMS').write_text(''.join(hashlib.sha256(q.read_bytes()).hexdigest()+'  '+str(q.relative_to(p))+'\n' for q in sorted(p.rglob('*')) if q.is_file() and q.name!='SHA256SUMS'))


if __name__=='__main__':main()
