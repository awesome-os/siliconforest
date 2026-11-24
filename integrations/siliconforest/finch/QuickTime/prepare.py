#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Create a fresh private failure-contract stage, not a system installer."""
import argparse,hashlib,json,shutil
from pathlib import Path
ap=argparse.ArgumentParser(description=__doc__)
for name in ['providers','mediatoolbox','tests','swift-tests','stage']:
 ap.add_argument('--'+name,type=Path,required=True)
a=ap.parse_args()
if a.stage.exists():raise ValueError('Fresh stage required')
(a.stage/'System/Library').mkdir(parents=True);(a.stage/'bin').mkdir()
shutil.copytree(a.providers/'System/Library/PrivateFrameworks',a.stage/'System/Library/PrivateFrameworks',symlinks=True)
shutil.copytree(a.mediatoolbox/'System/Library/Frameworks',a.stage/'System/Library/Frameworks',symlinks=True)
for source in [a.tests,a.swift_tests]:
 for p in (source/'bin').iterdir():shutil.copy2(p,a.stage/'bin'/p.name)
shutil.copy2(Path(__file__).resolve().parents[1]/'tools/compare-tests.py',a.stage/'compare-tests-engine.py')
shutil.copy2(Path(__file__).with_name('capture.py'),a.stage/'compare-tests.py')
rows=[]
for name in ['qt-private-probe','qt-private-swift-probe']:
 rows.append(dict(name=name,argv=['bin/'+name],mode='exit',skip_lines=0,timeout=20,
  env={'DYLD_FRAMEWORK_PATH':'@ROOT@/System/Library/PrivateFrameworks:@ROOT@/System/Library/Frameworks','SF_QT_PD_ONLY':'1'}))
(a.stage/'manifest.json').write_text(json.dumps(rows,indent=2)+'\n')
records=[]
for p in sorted(a.stage.rglob('*')):
 if p.is_file() and not p.is_symlink():records.append(dict(path=str(p.relative_to(a.stage)),sha256=hashlib.file_digest(p.open('rb'),'sha256').hexdigest()))
(a.stage/'cold-files.json').write_text(json.dumps(records,indent=2)+'\n')
print(a.stage)
