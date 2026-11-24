#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fresh staged copy for a bounded QuickTime load attempt; never alter the app."""
import argparse,hashlib,json,shutil
from pathlib import Path
ap=argparse.ArgumentParser(description=__doc__)
for name in ['app','providers','mediatoolbox','stage']:
 ap.add_argument('--'+name,type=Path,required=True)
ap.add_argument('--framework-overlay',type=Path,action='append',default=[])
ap.add_argument('--additional-framework',type=Path,action='append',default=[])
a=ap.parse_args()
if a.stage.exists() or a.app.name!='QuickTime Player.app':raise ValueError('Named app and fresh stage required')
(a.stage/'Applications').mkdir(parents=True)
shutil.copytree(a.app,a.stage/'Applications'/a.app.name,symlinks=True)
(a.stage/'System/Library').mkdir(parents=True)
shutil.copytree(a.providers/'System/Library/PrivateFrameworks',a.stage/'System/Library/PrivateFrameworks',symlinks=True)
for source in [a.mediatoolbox,*a.framework_overlay]:
 for p in [a.stage,*a.stage.rglob('*')]:
  if not p.is_symlink():p.chmod(p.stat().st_mode|0o200)
 shutil.copytree(source/'System/Library/Frameworks',a.stage/'System/Library/Frameworks',symlinks=True,dirs_exist_ok=True)
for source in a.additional_framework:
 if source.suffix!='.framework' or not source.is_dir():raise ValueError('Named extra framework required')
 for p in [a.stage,*a.stage.rglob('*')]:
  if not p.is_symlink():p.chmod(p.stat().st_mode|0o200)
 target=a.stage/'System/Library/Frameworks'/source.name
 if target.exists():raise ValueError('Extra framework would overwrite a selected provider: '+source.name)
 shutil.copytree(source,target,symlinks=True)
shutil.copy2(Path(__file__).with_name('load.py'),a.stage)
records=[]
for p in sorted(a.stage.rglob('*')):
 if p.is_file() and not p.is_symlink():records.append(dict(path=str(p.relative_to(a.stage)),sha256=hashlib.file_digest(p.open('rb'),'sha256').hexdigest()))
(a.stage/'cold-files.json').write_text(json.dumps(records,indent=2)+'\n')
(a.stage/'providers.json').write_text(json.dumps(dict(providers=str(a.providers.resolve()),mediatoolbox=str(a.mediatoolbox.resolve()),overlays=[str(p.resolve()) for p in a.framework_overlay],additional_frameworks=[str(p.resolve()) for p in a.additional_framework],limits='Fresh copy only; no client load-command, signature, bundle identity or source modification.'),indent=2)+'\n')
print(a.stage)
