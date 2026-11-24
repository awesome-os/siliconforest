#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Audit Finch PDFKit's real CGPDF calls against retained PD Mach-O exports.

Constants are listed separately: enum identifiers have no Mach-O symbol. This
is a source/import prerequisite audit, not a framework build or execution test.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import runpy


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source', type=Path, required=True)
    ap.add_argument('--provider', type=Path, action='append', required=True)
    ap.add_argument('--output', type=Path, required=True)
    a = ap.parse_args()
    macho = runpy.run_path(str(Path(__file__).resolve().parents[2]/'homebrew/hb_macho.py'))['MachO']
    text = a.source.read_text()
    calls = set(re.findall(r'\b(CGPDF\w+)\s*\(', text))
    constants = set(re.findall(r'\b(kCGPDF\w+)', text))
    exports = set()
    providers = []
    for p in a.provider:
        data = p.read_bytes()
        symbols = set(macho(data).exported()[0])
        exports.update(s[1:] for s in symbols if s.startswith('_'))
        providers.append(dict(path=str(p), sha256=hashlib.sha256(data).hexdigest(),
            pdf_exports=sorted(s for s in symbols if s.startswith(('_CGPDF', '_O2PDF', '_kCGPDF')))))
    result = dict(source='Finch 2d9b1bb PDFKit.m', providers=providers,
        functions=dict(required=sorted(calls), provided=sorted(calls & exports), missing=sorted(calls-exports)),
        constant_identifiers=sorted(constants))
    a.output.write_text(json.dumps(result, indent=2)+'\n')
    print(f'CGPDF calls {len(calls)}, provided {len(calls & exports)}, missing {len(calls-exports)}')


if __name__ == '__main__':
    main()
