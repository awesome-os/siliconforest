#!/usr/bin/env python3
"""Standalone host protocol and sanitizer checks. No device or target execution."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='dl-spihid-') as directory:
    output = Path(directory) / 'protocol-tests'
    compiler = shutil.which('clang') or shutil.which('cc')
    if compiler is None:
        raise SystemExit('No host C compiler')
    subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    str(root/'dl_spi_hid.c'), str(root/'test_protocol.c'), '-o', str(output)], check=True)
    subprocess.run([str(output)], check=True, timeout=30,
                   env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
