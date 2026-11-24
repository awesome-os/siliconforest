#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Find real function definitions in preprocessed Finch C, including its macros.

The input is produced with our type headers and the open PD sysroot. Only
non-static definition signatures are emitted; no SDK declarations are copied.
"""
import argparse
from pathlib import Path
import re


import runpy
import sys
definitions = runpy.run_path(sys.argv.pop(1))["definitions"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('inputs', type=Path, nargs='+')
    args = parser.parse_args()
    found = {}
    for path in args.inputs:
        for ret,name,params in definitions(path.read_text()):
            if name.startswith(('vimage_','la_object_')): continue
            if name.startswith(('v','finch_','_Sparse','Sparse','sparse_','la_','quadrature_')) or re.fullmatch(r'[sdcz][a-z0-9]+_',name):
                signature = f'{ret} {name}({params})'+(' SF_LA_RETAINED' if ret=='la_object_t' else '')+';'
                if name in found and found[name] != signature:
                    raise ValueError('Inconsistent declaration: '+name)
                found[name] = signature
    if not found:
        raise ValueError('No Finch definitions extracted')
    args.output.write_text('/* SPDX-License-Identifier: MIT OR Apache-2.0 */\n/* Declarations derived from preprocessed Finch function definitions. */\n#pragma once\n'+'\n'.join(found[n] for n in sorted(found))+'\n')


if __name__ == '__main__':
    main()
