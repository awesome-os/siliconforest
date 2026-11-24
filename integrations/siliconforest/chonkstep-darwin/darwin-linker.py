#!/usr/bin/env python3
"""Link the ARM64 Rust backend against the pinned SiliconForest OSS runtime."""
import os
from pathlib import Path
import sys

STORE = Path('/nix/store')
toolchain = STORE / 'v0x4y2cjbasgmrqsbka2bp8allw8j9ph-darwin-cross-toolchain-nix'
ld = STORE / 'n19v2yzd05pirxz38xjhghknchx3fg1i-puredarwin-native-ld-0.1/bin/ld'
sdk = STORE / '6zpwyx6p9swad42f0vx6rl4z0dp2kj1g-puredarwin-oss-sdk-26.5/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk'
system = STORE / 'jll17vb2bl849knd0b68n358c53wh1gl-puredarwin-libsystem-0.1/usr/lib'
sfgop = STORE / 'nqi47vjchbk1yv29mz7kkxbfcmz7zhp5-puredarwin-chonkstep-pdgop-static-0.1/lib'
xkb = STORE / '5shniy0j07mkrl7kys7vslm9vh02lw6d-puredarwin-libxkbcommon-nox-1.13.1/lib'
iconv = STORE / 'z9qr7jl3d8yy8pniaai1mz79svj7ng92-puredarwin-libiconv-1.19/usr/lib'
cc = toolchain / 'bin/arm64-apple-darwin20.4-clang'
for path in (cc, ld, sdk, system, sfgop / 'libSFGOP.a', sfgop / 'libIOKit.a', xkb / 'libxkbcommon.a'):
    if not path.exists():
        raise SystemExit('Missing pinned link input: ' + str(path))
env = dict(os.environ, DARWIN_SDK_ROOT=str(sdk), SDKROOT=str(sdk))
flags = [str(cc), '-fuse-ld=' + str(ld), '-nostdlib', '-L' + str(system),
         '-L' + str(system / 'system'), '-L' + str(sfgop), '-L' + str(xkb), '-L' + str(iconv),
         '-Wl,-dylib_file,/usr/lib/system/libdyld.dylib:' + str(system / 'system/libdyld.dylib'),
         '-Wl,-platform_version,macos,26.5,26.5', '-Wl,-fixup_chains']
os.execve(cc, flags + sys.argv[1:], env)
