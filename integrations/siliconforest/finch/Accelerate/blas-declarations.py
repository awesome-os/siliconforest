#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Author BLAS ABI wrapper inputs from Finch's own preprocessed signatures.

The public integer width and legacy single-real return rule are explicit; no
SDK headers, stub libraries or symbol lists are consumed.
"""
from pathlib import Path
import re
import sys

source=Path(sys.argv[1]).read_text()
out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
blas=[];cblas=[];legacy=[];symbols=set()
for ret,name,args in re.findall(r'^([\w ]+?) (finch_\w+)\(([^;]*)\);',source,re.M):
    public=name[len('finch_'):]
    if public.startswith('cblas_v') or public=='SetBLASParamErrorProc':continue
    if not (public.startswith(('cblas_','appleblas_')) or public.endswith('_')):continue
    ret=re.sub(r'\blong\b','__LAPACK_int',ret)
    args=re.sub(r'\blong\b','__LAPACK_int',args)
    declaration=f'{ret} {public}({args});'
    if public.startswith(('cblas_','appleblas_')):
        cblas.append(declaration)
        if re.fullmatch(r'cblas_[sdcz](axpby|set)',public):
            alias=public.replace('cblas_','catlas_',1)
            cblas.append(declaration.replace(public,alias,1))
            symbols.update('_'+alias+suffix for suffix in ['', '$NEWLAPACK', '$NEWLAPACK$ILP64'])
        for suffix in ['', '$NEWLAPACK', '$NEWLAPACK$ILP64']:symbols.add('_'+public+suffix)
    else:
        blas.append(declaration)
        legacy.append(f'{"double" if ret=="float" else ret} {public}({args});')
        stem=public[:-1]
        symbols.update('_'+n for n in [public,stem,stem.upper(),stem+'$NEWLAPACK',stem+'$NEWLAPACK$ILP64'])
for ret,n,args in [
 ('void','cblas_xerbla','__LAPACK_int p, const char *routine, const char *form, ...'),
 ('__LAPACK_int','cblas_errprn','__LAPACK_int ierr, __LAPACK_int info, const char *form, ...')]:
    cblas.append(f'{ret} {n}({args});')
    symbols.update('_'+n+s for s in ['', '$NEWLAPACK', '$NEWLAPACK$ILP64'])
notice='/* SPDX-License-Identifier: MIT OR Apache-2.0 */\n/* From Finch implementation signatures, never an SDK. */\n'
for name,lines in [('blas_new.h',blas),('cblas_new.h',cblas),('fortran_blas.h',legacy)]:
    if not lines:raise ValueError('No BLAS declarations: '+name)
    (out/name).write_text(notice+'\n'.join(sorted(lines))+'\n')
(out/'symbols').write_text('exports:\n'+'\n'.join(sorted(symbols))+'\n')
# Public test/client declarations select the ABI explicitly at compile time.
# CBLAS numeric constants are the constants in Finch's blas.c implementation.
public = [notice, '#pragma once',
 'typedef float _Complex __LAPACK_float_complex;',
 'typedef double _Complex __LAPACK_double_complex;',
 '#include <Accelerate/CBLAS.h>']
for modern, suffix in [(False,''),(True,'$NEWLAPACK')]:
    public.append('#ifdef ACCELERATE_NEW_LAPACK' if modern else '#ifndef ACCELERATE_NEW_LAPACK')
    for line in sorted(cblas+(blas if modern else legacy)):
        m=re.fullmatch(r'(.+?) (\w+)\((.*)\);',line)
        ret,name,args=m.groups()
        symbol=name+suffix if name.startswith(('cblas_','catlas_','appleblas_')) else name[:-1]+suffix if modern else name
        public.append(f'{ret} {name}({args}) __asm("_{symbol}");')
    public.append('#endif')
Path(sys.argv[1]).parent.joinpath('BLAS-public.h').write_text('\n'.join(public)+'\n')
