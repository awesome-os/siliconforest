#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run staged platform sync/free/decode bodies with instrumented native API stand-ins."""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

here = Path(__file__).resolve().parent
avd = Path(sys.argv[1]) / 'src/Kernel/Extensions/SFAppleAVD'
s = (avd / 'kernel/SFAppleAVD.cpp').read_text()
def block(start, semi=False):
    start = s.index(start); brace = s.index('{', start)
    lex = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"', lambda m: ' '*len(m[0]), s, flags=re.S)
    depth = 0
    for i in range(brace, len(s)):
        depth += (lex[i] == '{') - (lex[i] == '}')
        if not depth: return s[start:i+1] + (';' if semi else '')
    raise ValueError('unbalanced')
actual = block('struct DmaCookie {',True) + '\n' + block('struct Context {',True) + '\n'
actual += 'static DmaCookie *gQuarantined;\nstatic Context context;\n'
actual += 'static Context *context_of(uint32_t){return &context;}\n'
actual += block('extern "C" int\npd_avd_os_dma_sync(') + '\n'
actual += block('static void quarantine_dma(') + '\n'
actual += block('extern "C" void\npd_avd_os_dma_free(') + '\n'
actual += block('static int\ndo_decode(') + '\n'
with tempfile.TemporaryDirectory(prefix='sf-avd-native-') as tmp:
    tmp=Path(tmp); header=tmp/'actual.h'; header.write_text(actual)
    binary=tmp/'native'
    subprocess.run([os.environ.get('CXX','clang++'),'-std=c++14','-O1','-g','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-fno-sanitize-recover=all',
                    '-I'+str(avd/'include'),'-I'+str(avd/'kernel'),
                    '-DSF_NATIVE_UNIT_HEADER="'+str(header)+'"',
                    str(here/'native-ownership.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=15)
