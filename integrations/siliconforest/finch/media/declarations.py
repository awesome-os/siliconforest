#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Public C declarations from preprocessed Finch definitions and source constants."""
from pathlib import Path
import runpy,re,sys
find=runpy.run_path(sys.argv[1])['definitions']
source,out,prefix=Path(sys.argv[2]),Path(sys.argv[3]),sys.argv[4]
functions={}
for path in map(Path,sys.argv[5:]):
    for ret,name,args in find(path.read_text()):
        if name.startswith(tuple(prefix.split(','))):
            declaration=f'{ret} {name}({args});'
            if 'id<' in declaration:declaration='#ifdef __OBJC__\n'+declaration+'\n#else\n'+re.sub(r'id<[^>]+>', 'void *', declaration)+'\n#endif'
            functions[name]=declaration
constants={}
for path in source.glob('*.m'):
    for name in re.findall(r'^NSString \*const (k\w+)\s*=',path.read_text(),re.M):constants[name]=f'extern const CFStringRef {name};'
# Typed constants defined by the implementation (time/range/tag and plain scalars).
for path in source.glob('*.c'):
    for typ,name in re.findall(r'^const (\w+) ((?:kCM|kCV)\w+)\s*=',path.read_text(),re.M):constants[name]=f'extern const {typ} {name};'
lines=['/* SPDX-License-Identifier: MIT OR Apache-2.0 */','#pragma once',*map(constants.get,sorted(constants)),*map(functions.get,sorted(functions))]
if not functions:raise ValueError('No public functions found')
out.write_text('\n'.join(lines)+'\n')
