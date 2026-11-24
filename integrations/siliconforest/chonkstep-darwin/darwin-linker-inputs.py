#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Link the ARM64 Rust desktop binaries against the SiliconForest runtime named by a file.

darwin-linker.py with its seven store paths read from the JSON file DARWIN_LINK_INPUTS names
(link-inputs.nix builds that file for the source rebase/pd.nix selects) instead of written
here. Same command line otherwise."""
import json
import os
from pathlib import Path
import sys

where = os.environ.get('DARWIN_LINK_INPUTS')
if not where:
    raise SystemExit('DARWIN_LINK_INPUTS is not set (nix-build link-inputs.nix)')
inputs = json.loads(Path(where).read_text())
toolchain, ld, sdk, system, sfgop, xkb, iconv = (
    Path(inputs[key]) for key in ('toolchain', 'ld', 'sdk', 'system', 'sfgop', 'xkb', 'iconv'))
cc = toolchain / 'bin/arm64-apple-darwin20.4-clang'
for path in (cc, ld, sdk, system, sfgop / 'libSFGOP.a', sfgop / 'libIOKit.a', xkb / 'libxkbcommon.a'):
    if not path.exists():
        raise SystemExit('Missing link input: ' + str(path))
env = dict(os.environ, DARWIN_SDK_ROOT=str(sdk), SDKROOT=str(sdk))
flags = [str(cc), '-fuse-ld=' + str(ld), '-nostdlib', '-L' + str(system),
         '-L' + str(system / 'system'), '-L' + str(sfgop), '-L' + str(xkb), '-L' + str(iconv),
         '-Wl,-dylib_file,/usr/lib/system/libdyld.dylib:' + str(system / 'system/libdyld.dylib'),
         '-Wl,-platform_version,macos,26.5,26.5', '-Wl,-fixup_chains']
os.execve(cc, flags + sys.argv[1:], env)
