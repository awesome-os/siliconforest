#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Own class/property encodings from source and maintainer public API facts, no SDK."""
from pathlib import Path
import re,json,sys
src,facts,dest=map(Path,sys.argv[1:4])
text='\n'.join(p.read_text() for p in src.glob('*.m'));private=set(re.findall(r'@interface\s+(\w+)\s*:',text));names=set(re.findall(r'@implementation\s+(CI\w+)',text))-private
rust='\n'.join(p.read_text() for p in facts.glob('*.rs'));classes={}
for n in sorted(names):
 m=re.search(r'#\[unsafe\(super\(([^,)]+)[^\n]*\)\)\](?:(?!#\[unsafe\(super).)*?pub struct '+n+r'(?:[;<])',rust,re.S)
 if not m:raise ValueError(n)
 classes[n]=m[1].strip()
ordered={}
while classes:
 for n,b in list(classes.items()):
  if b not in classes:ordered[n]=b;del classes[n]
(dest/'classes.json').write_text(json.dumps(ordered,indent=2)+'\n')
builtins=(facts/'CIFilterBuiltins.rs').read_text();wanted=re.findall(r'X\((CI\w+)\)',(src/'CIFilterBuiltins.inc').read_text())
def ctype(r):
 r=r.strip()
 while r.startswith(('Option<','Retained<','NonNull<')):r=r[r.index('<')+1:-1].strip()
 basic={'c_float':'float','c_double':'double','bool':'BOOL','NSInteger':'NSInteger','NSUInteger':'NSUInteger','CGFloat':'CGFloat','CGPoint':'CGPoint','CGSize':'CGSize','CGRect':'CGRect','CGAffineTransform':'CGAffineTransform','i32':'int32_t','u32':'uint32_t'}
 if r in basic:return basic[r]
 if r=='CIDynamicRangeOption':return 'NSString *'
 if r in ['CGColorSpace','CGImage']:return r+'Ref'
 if re.fullmatch(r'(?:CI|NS)\w+',r):return r+' *'
 raise ValueError('Unknown property type: '+r)
lines=['/* SPDX-License-Identifier: MIT */','/* Own property encodings from public maintainer binding facts (objc2-core-image0.3.2). */','#pragma once','#import <CoreImage/CoreImage.h>','@protocol CIFilterProtocol <NSObject>','@property(readonly) CIImage *outputImage;','@end']
done={'CIFilterProtocol'}
def emit(n):
 if n in done:return
 m=re.search(r'pub unsafe trait '+n+r':\s*(\w+)\s*\{(.*?)\n    \}\n\);',builtins,re.S)
 if not m:raise ValueError('Missing protocol fact '+n)
 base=m[1]
 if base=='NSObjectProtocol':base='NSObject'
 if base!='NSObject':emit(base)
 done.add(n)
 lines.append('@protocol '+n+' <'+base+'>')
 body=re.sub(r'///[^\n]*','',m[2]);props=re.findall(r'#\[unsafe\(method\((\w+)\)\)\](?:(?!#\[unsafe\(method\().)*?(?:unsafe )?fn \w+\(&self\) -> ([^;]+);',body,re.S)
 for prop,r in props:
  writable='set'+prop[0].upper()+prop[1:]+':' in body
  lines.append('@property'+('' if writable else '(readonly)')+' '+ctype(r)+' '+prop+';')
 lines.append('@end')

for n in wanted:emit(n)
(dest/'include/CoreImage/CIFilterBuiltins.h').write_text('\n'.join(lines)+'\n');(dest/'include/CoreImage/BuiltinProtocols.h').write_text('/* SPDX-License-Identifier: MIT */\n#pragma once\n@protocol CIFilterProtocol,'+','.join(wanted)+';\n')
print(len(ordered),'classes,',len(wanted),'typed built-in protocols')

# Factories are declared only where Finch's selector resolver has a real table entry.
filter_names={n.lower():n for n in re.findall(r'^\{"(CI\w+)"', (src/'CIFilterSpecs.inc').read_text(),re.M)}
selectors=re.findall(r'#\[unsafe\(method\((\w+)\)\)\](?:(?!#\[unsafe\(method\().)*?pub unsafe fn \w+\(\) -> Retained<CIFilter>;',builtins,re.S)
factory_lines=['/* SPDX-License-Identifier: MIT */','#pragma once','@interface CIFilter (SFBuiltinFactories)']
for selector in selectors:
 base=selector[:-6] if selector.endswith('Filter') else selector
 candidates=['CI'+base]+(['CI'+base+'Filter'] if selector.endswith('Filter') else [])
 name=next((filter_names[n.lower()] for n in candidates if n.lower() in filter_names),None)
 if name:
  result='CIFilter<'+name+'> *' if name in wanted else 'CIFilter *'
  factory_lines.append('+ ('+result+')'+selector+';')
factory_lines.append('@end')
(dest/'include/CoreImage/CIFilterFactories.h').write_text('\n'.join(factory_lines)+'\n')
