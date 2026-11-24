#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Write numeric API facts from maintainer Rust bindings; never accepts SDK headers."""
from pathlib import Path
import re,sys
root,out,prefix=Path(sys.argv[1]),Path(sys.argv[2]),sys.argv[3]
values={}
for path in sorted(root.glob('*.rs')):
    text=path.read_text()
    for name,value in re.findall(r'pub const (\w+):\s*[\w:]+\s*=\s*([^;]+);',text):
        if name.startswith(prefix) and re.fullmatch(r'[-+\d\sxa-fA-F_()<>|&]+',value):values[name]=value
    for name,value in re.findall(r'#\[doc\(alias = "(\w+)"\)\]\s*(?:#\[[^\n]+\]\s*)*(?:pub )?const \w+(?:: Self)? = (?:Self\()?([-+\d\sxa-fA-F_<>|&]+)\)?;',text):
        if name.startswith(prefix):values[name]=value
lines=['/* SPDX-License-Identifier: MIT */','/* Numeric facts checked against objc2 framework bindings 0.3.2; our declarations. */','#pragma once']
for name,value in sorted(values.items()):
    value=value.replace('_','')
    value=re.sub(r'\b1\s*<<','1ULL <<',value)
    lines.append('#define '+name+' ('+value.strip()+')')
out.write_text('\n'.join(lines)+'\n');print('numeric facts',len(values))
