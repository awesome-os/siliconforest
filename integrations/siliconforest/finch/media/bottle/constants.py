#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Select named CFString definitions from pinned Finch, preserving its SPDX notice."""
from pathlib import Path
import re
import sys
source, names, out = map(Path, sys.argv[1:4])
wanted=set(names.read_text().splitlines())
values={}
for file in [source/'VTConstants.c',source/'VideoToolboxConstants_Generated.m']:
    text=file.read_text()
    for name,value in re.findall(r'\b(kVT\w+)\s*=\s*CFSTR\("([^"]*)"\)',text):values[name]=value
    for name,value in re.findall(r'VT_PROFILE\((kVT\w+),\s*"([^"]*)"\)',text):values[name]=value
missing=wanted-set(values)
if missing:raise ValueError('No pinned definition: '+','.join(sorted(missing)))
lines=['/* SPDX-License-Identifier: MIT OR Apache-2.0 */','/* Selected from Finch2d9b1bb VTConstants.c and generated constants. */','#include <CoreFoundation/CoreFoundation.h>']
lines += [f'const CFStringRef {name}=CFSTR("{values[name]}");' for name in sorted(wanted)]
out.write_text('\n'.join(lines)+'\n')
