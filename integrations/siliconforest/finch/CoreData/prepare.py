#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fresh isolated CoreData stage and reviewed binaries-only native reference packet."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for n in ['framework','tests','stage','reference-packet']:
        ap.add_argument('--'+n,type=Path,required=True)
    a=ap.parse_args();here=Path(__file__).resolve().parent
    if a.stage.exists() or a.reference_packet.exists():raise ValueError('Destinations must be new')
    fw=a.stage/'System/Library/Frameworks';fw.mkdir(parents=True)
    shutil.copytree(a.framework/'System/Library/Frameworks/CoreData.framework',fw/'CoreData.framework',symlinks=True)
    shutil.copytree(a.tests/'bin',a.stage/'bin');(a.stage/'bin').chmod(0o755)
    subprocess.run([sys.executable,str(here.parent/'tools/portable-tests.py'),str(here.parent.parent/'homebrew/hb_macho.py'),str(a.stage/'bin'),str(a.stage/'binary-hashes.json')],check=True)
    rows=[dict(name=n,argv=['bin/'+n]+(['fresh.sqlite'] if n=='coredata-sqlite-probe' else []),mode='reference',skip_lines=0,timeout=30,env={'DYLD_FRAMEWORK_PATH':'@ROOT@/System/Library/Frameworks'}) for n in ['coredata-sqlite-probe','coredata-hash-test']]
    (a.stage/'manifest.json').write_text(json.dumps(rows,indent=2)+'\n');shutil.copy2(here.parent/'tools/compare-tests.py',a.stage)
    p=a.reference_packet;p.mkdir(parents=True);shutil.copytree(a.stage/'bin',p/'bin');(p/'bin').chmod(0o755)
    (p/'manifest.json').write_text(json.dumps([{k:v for k,v in r.items() if k!='env'} for r in rows],indent=2)+'\n');shutil.copy2(here.parent/'tools/compare-tests.py',p)
    (p/'SHA256SUMS').write_text(''.join(hashlib.sha256(q.read_bytes()).hexdigest()+'  '+str(q.relative_to(p))+'\n' for q in sorted(p.rglob('*')) if q.is_file() and q.name!='SHA256SUMS'))


if __name__=='__main__':main()
