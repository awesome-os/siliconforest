#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Public declarations from Finch definitions, not an SDK."""
from pathlib import Path
import re,runpy,sys
find=runpy.run_path(sys.argv[1])['definitions']
source,out=Path(sys.argv[2]),Path(sys.argv[3]);found={}
for path in map(Path,sys.argv[4:]):
 for ret,name,args in find(path.read_text()):
  if name.startswith(('ColorSync','CGDisplay')):found[name]=f'{ret} {name}({args});'
text='\n'.join(p.read_text() for p in source.glob('*.c'))
constants={name:f'extern {typ} {name};' for typ,name in re.findall(r'^(const uint64_t|CFStringRef) (kColorSync\w+)\s*=',text,re.M)}
if not found or not constants:raise ValueError('No ColorSync declarations')
out.write_text('/* SPDX-License-Identifier: MIT OR Apache-2.0 */\n#pragma once\n'+'\n'.join(constants[n] for n in sorted(constants))+'\n'+'\n'.join(found[n] for n in sorted(found))+'\n')
