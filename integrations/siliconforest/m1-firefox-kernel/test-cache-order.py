#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile actual staged decode/grey bodies; ordering and failure injection only."""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

here = Path(__file__).resolve().parent
kernel = Path(sys.argv[1]) / 'src/Kernel/Extensions/SFAppleAVD/kernel'
source = (kernel / 'sf_avd_core.c').read_text()

def block(start, semi=False):
    begin = source.index(start)
    brace = source.index('{', begin)
    lexical = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"',
                     lambda m: ' ' * len(m[0]), source, flags=re.S)
    depth = 0
    for i in range(brace, len(source)):
        depth += (lexical[i] == '{') - (lexical[i] == '}')
        if depth == 0:
            return source[begin:i+1] + (';' if semi else '')
    raise ValueError(start)

prefix = '''#include "sf_linux_shim.h"
#include "linux/avd.h"
#include "linux/avd-regs.h"
#include "sf_avd_core.h"
static int avd_reset(struct sf_avd_device *dev);
'''
actual = (prefix + block('struct sf_avd_device {', True) + '\n' +
          block('struct sf_avd_session {', True) + '\n' +
          block('static void grey_chroma(') + '\n' +
          block('int sf_avd_session_decode(') + '\n')
# Counterexamples must fail the same hosted ordering assertions.
no_before = actual.replace('ret = sf_avd_os_dma_sync(capture_cookie, SF_AVD_DMA_BEFORE_DEVICE);', 'ret = 0;')
assert no_before != actual
no_after = actual.replace('ret = sf_avd_os_dma_sync(capture_cookie, SF_AVD_DMA_AFTER_DEVICE);', 'ret = 0;')
assert no_after != actual
with tempfile.TemporaryDirectory(prefix='sf-firefox-cache-') as tmp:
    tmp = Path(tmp)
    for name, code, negative in [('actual', actual, False), ('missing-before', no_before, True), ('missing-after', no_after, True)]:
        header = tmp / (name + '.h'); header.write_text(code)
        binary = tmp / name
        subprocess.run([os.environ.get('CC', 'clang'), '-std=gnu11', '-O1', '-g',
                        '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        '-I'+str(kernel), '-I'+str(kernel/'shim'),
                        '-DSF_CACHE_UNIT_HEADER="'+str(header)+'"',
                        str(here/'cache-order.c'), '-o', str(binary)], check=True)
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
        if negative:
            if run.returncode == 0 or 'count == n' not in run.stderr:
                raise SystemExit(name + ' did not fail its expected ordering assertion: '+run.stderr)
            print(name + ': ordering counterexample rejected', flush=True)
        else:
            print(run.stdout, end='', flush=True)
            if run.returncode:
                print(run.stderr, file=sys.stderr)
                raise SystemExit(run.returncode)
