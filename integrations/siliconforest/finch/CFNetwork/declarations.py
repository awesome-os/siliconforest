#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Derive CFNetwork C signatures from its real preprocessed definitions."""
import argparse
from pathlib import Path
import re
import sys
import runpy
definitions = runpy.run_path(sys.argv.pop(1))["definitions"]

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('source',type=Path)
p.add_argument('output',type=Path)
p.add_argument('inputs',type=Path,nargs='+')
a=p.parse_args()
found={}
for f in a.inputs:
    for ret,name,args in definitions(f.read_text()):
        if name.startswith(('CF','_CF')):
            found[name]=ret+' '+name+'('+args+');'
constants={}
for f in a.source.glob('*.c'):
    for typ,name in re.findall(r'^const\s+(\w+)\s+(\w+)\s*=',f.read_text(),re.M):
        constants[name]='extern const '+typ+' '+name+';'
a.output.write_text('/* SPDX-License-Identifier: MIT OR Apache-2.0 */\n/* Signatures/constants from Finch source; no SDK. */\n#pragma once\n'+'\n'.join(constants[n] for n in sorted(constants))+'\n'+'\n'.join(found[n] for n in sorted(found))+'\n')
