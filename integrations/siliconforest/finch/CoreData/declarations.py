#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Own declarations from pinned Finch implementations and reviewed type facts."""
from pathlib import Path
import subprocess,sys,json,re
here=Path(__file__).resolve().parent
classes=json.loads((here/'classes.json').read_text());out=here/'include/CoreData/CoreData.h'
subprocess.run([sys.executable,str(here.parent/'tools/declarations.py'),sys.argv[1],str(here/'types.h'),str(out),'--class-map',','.join(k+':'+v for k,v in classes.items()),'--categories'],check=True)
s=out.read_text()
for n in ['NSFetchRequest','NSAsynchronousFetchResult']:
 s=s.replace('@interface '+n+' :','@interface '+n+'<__covariant ResultType> :')
 # Parameterized forward declaration is needed before asynchronous request signatures.
 where=s.index('\n',s.index('@class '))+1
 s=s[:where]+'@class '+n+'<__covariant ResultType>;\n'+s[where:]
for name in classes:
 start=s.index('@interface '+name+('<__covariant ResultType> :' if name in ['NSFetchRequest','NSAsynchronousFetchResult'] else ' :'));end=s.index('@end',start);body=s[start:end]
 for method in re.findall(r'- \(BOOL\)(is[A-Z]\w*)\s*;',body):
  prop=method[2].lower()+method[3:];setter='set'+method[2:]+':'
  readonly='' if setter in body else ',readonly'
  body+='@property(getter='+method+readonly+') BOOL '+prop+';\n'
 s=s[:start]+body+s[end:]
# Public marker protocol; methods remain the source's declarations.
s=s.replace('@interface NSManagedObject : NSObject','@interface NSManagedObject : NSObject <NSFetchRequestResult>')
s=s.replace('@interface NSManagedObjectID : NSObject','@interface NSManagedObjectID : NSObject <NSFetchRequestResult,NSCopying>')
for path in Path(sys.argv[1]).glob('*.m'):
 for n in re.findall(r'NSString\s*\*\s*const\s+(\w+)\s*=',path.read_text()):
  if not re.search(r'extern NSString\s*\*\s*const\s+'+n+r'\s*;',s):s+='\nextern NSString *const '+n+';\n'
out.write_text(s)
for name in classes:(out.parent/(name+'.h')).write_text('/* SPDX-License-Identifier: MIT */\n#pragma once\n#import <CoreData/CoreData.h>\n')
