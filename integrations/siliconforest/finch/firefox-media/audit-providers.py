#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Audit effective local reexports against retained Linux install artifacts."""
import argparse,hashlib,json,runpy
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--package',type=Path,required=True)
p.add_argument('--corevideo-baseline',type=Path)
p.add_argument('--videotoolbox-baseline',type=Path)
p.add_argument('--audiotoolbox-baseline',type=Path)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();here=Path(__file__).resolve().parent
api=runpy.run_path(str(here.parents[1]/'homebrew/hb_macho.py'))
def effective(path,ancestors=()):
    path=path.resolve()
    if path in ancestors:raise ValueError('Reexport cycle')
    image=api['MachO'](path.read_bytes(),str(path));own,reexports=image.exported()
    result=own|set(reexports)
    for library in image.dylibs:
        if library['kind']!='reexport':continue
        name=library['name']
        if not name.startswith('@loader_path/'):raise ValueError('Unresolved reexport: '+name)
        child=path.parent/name[len('@loader_path/'):]
        if not child.resolve().is_relative_to(path.parent):raise ValueError('Reexport outside this owned framework')
        result|=effective(child,(*ancestors,path))
    return result
rows=[]
baselines=[('CoreVideo',a.corevideo_baseline),('VideoToolbox',a.videotoolbox_baseline),('AudioToolbox',a.audiotoolbox_baseline)]
if not any(path is not None for _,path in baselines):p.error('At least one baseline required')
for name,baseline in baselines:
    if baseline is None:continue
    candidate=a.package/'Frameworks'/(name+'.framework/Versions/A/'+name)
    before=effective(baseline);after=effective(candidate)
    rows.append(dict(framework=name,baseline=str(baseline),candidate=str(candidate),
                     baseline_sha256=hashlib.sha256(baseline.read_bytes()).hexdigest(),
                     candidate_sha256=hashlib.sha256(candidate.read_bytes()).hexdigest(),
                     baseline_exports=len(before),candidate_effective_exports=len(after),
                     missing=sorted(before-after),added=sorted(after-before),
                     scope='Export superset, not opaque ABI or installed runtime qualification.'))
a.output.write_text(json.dumps(rows,indent=2)+'\n')
for row in rows:print(row['framework'],row['baseline_exports'],row['candidate_effective_exports'],'missing',len(row['missing']))
raise SystemExit(any(row['missing'] for row in rows))
