#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Negative M1 check: unsupported caption writing must preserve our input PNG.

This is not a positive caption-conformance test. Run the ordinary probe too;
its failing round-trip keeps the framework ineligible for installation.
"""
import argparse
from pathlib import Path
import subprocess

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('program', type=Path)
ap.add_argument('fixture', type=Path)
a = ap.parse_args()
before = a.fixture.read_bytes()
r = subprocess.run([str(a.program.resolve()), str(a.fixture.resolve())],
                   capture_output=True, text=True, timeout=30)
unchanged = a.fixture.read_bytes() == before
refused = r.returncode == 4 and 'caption own-PNG write=0 roundtrip=0' in r.stdout
print('unsupported caption refused='+str(int(refused))+' original-PNG-unchanged='+str(int(unchanged)))
if not refused or not unchanged:
    print(r.stdout, r.stderr)
    raise SystemExit(1)
