#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Source-derived CoreImage class declarations; own type and protocol facts."""
from pathlib import Path
import json,subprocess,sys,re
here=Path(__file__).resolve().parent;classes=json.loads((here/'classes.json').read_text());out=here/'include/CoreImage/CoreImage.h'
subprocess.run([sys.executable,str(here.parent/'tools/declarations.py'),sys.argv[1],str(here/'types.h'),str(out),'--class-map',','.join(n+':'+b for n,b in classes.items()),'--categories'],check=True)
s=out.read_text();s='\n'.join(line for line in s.splitlines() if "\\" not in line)+'\n';s=re.sub(r'^[-+] \([^)]*\)\s*_\w+[^;]*;\n','',s,flags=re.M)
for n in classes:
 a=s.index('@interface '+n+' :');b=s.index('@end',a);body=s[a:b]
 for method in re.findall(r'- \(BOOL\)(is[A-Z]\w*)\s*;',body):
  prop=method[2].lower()+method[3:];body+='@property(readonly,getter='+method+') BOOL '+prop+';\n'
 s=s[:a]+body+s[b:]
# Constants and macro-generated factories are defined by the pinned source.
s+='\n'
for p in Path(sys.argv[1]).glob('*.m'):
 for typ,n in re.findall(r'^const (CIFormat|int|double) (\w+)\s*=',p.read_text(),re.M):s+='extern const '+typ+' '+n+';\n'
for n in ['blackColor','whiteColor','grayColor','redColor','greenColor','blueColor','cyanColor','magentaColor','yellowColor','clearColor']:
 s+='\n@interface CIColor (SF'+n+')\n+ (CIColor *)'+n+';\n@end\n'
s+='\n@interface CIBlendKernel (SFNamedKernels)\n'
for name in re.findall(r'BLEND\((\w+)\)',(Path(sys.argv[1])/'CIMisc.m').read_text()):
 if name!='sel':s+='+ (CIBlendKernel *)'+name+';\n'
s+='@end\n#import <CoreImage/CIFilterFactories.h>\n'
out.write_text(s)
for n in classes:(out.parent/(n+'.h')).write_text('/* SPDX-License-Identifier: MIT */\n#pragma once\n#import <CoreImage/CoreImage.h>\n')
