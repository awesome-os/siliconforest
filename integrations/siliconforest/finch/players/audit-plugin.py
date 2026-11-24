#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Offline plugin audit in its actual VLC executable's rpath context.

Keep unresolved snapshot paths in the report. They are not evidence that the
installed target lacks a provider. This does not prove runtime selector/ABI
compatibility; the finite loader preflight and playback remain required.
"""
import argparse
import collections
import json
from pathlib import Path
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'homebrew'))
from sf_gapscan import Root,image_gaps

def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('--root',required=True,type=Path)
 ap.add_argument('--stage',required=True,type=Path)
 ap.add_argument('--mount',action='append',default=[],help='Logical absolute prefix=host directory')
 ap.add_argument('--plugin',required=True,help='A basename under Contents/MacOS/plugins')
 ap.add_argument('--output',required=True,type=Path)
 args=ap.parse_args()
 if Path(args.plugin).name!=args.plugin:raise ValueError('Plugin basename required')
 mounts=[('/System/Library/Frameworks',args.stage/'Frameworks')]
 for item in args.mount:
  logical,host=item.split('=',1)
  if not logical.startswith('/') or not Path(host).is_dir():raise ValueError('Invalid mount')
  mounts.append((logical,Path(host)))
 root=Root(args.root,overlays=[args.stage],mounts=mounts)
 exe='/Applications/VLC.app/Contents/MacOS/SiliconForest-VLC'
 entry=root.resolve(exe)
 if entry is None:raise ValueError('Missing actual staged entry program')
 logical='/Applications/VLC.app/Contents/MacOS/plugins/'+args.plugin
 host=root.resolve(logical)
 if host is None:raise ValueError('Missing selected plugin')
 todo=[(logical,host,root.macho(entry).rpaths)];seen=set();rows=[];images=[]
 while todo:
  name,path,search=todo.pop()
  if str(path) in seen:continue
  seen.add(str(path));images.append(name)
  gaps,dependencies,inherited=image_gaps(root,name,path,exe,search)
  rows.extend(dict(image=name,kind=k,library=lib,symbol=symbol,weak=weak)
              for k,lib,symbol,weak in sorted(gaps))
  todo.extend((dep,found,inherited) for dep,found in dependencies)
 direct=[r for r in rows if r['image']==logical and not r['weak']]
 grouped=collections.Counter(r['image'] for r in rows if not r['weak'])
 report=dict(executable=exe,rpaths=root.macho(entry).rpaths,plugin=logical,
             closure_images=sorted(images),direct_missing_strong=direct,
             closure_missing_strong_by_image=dict(sorted(grouped.items())),gaps=rows,
             limits='Snapshot gaps remain unresolved inventory; static symbols do not prove runtime method receivers or ABI.')
 args.output.write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps({k:report[k] for k in ['plugin','rpaths','direct_missing_strong','closure_missing_strong_by_image']},indent=2))
 return bool(direct)

if __name__=='__main__':raise SystemExit(main())
