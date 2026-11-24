#!/usr/bin/env python3
"""Build-only pinned SDK object. No fetch, Nix invocation, or execution."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
TOOLCHAIN = Path('/nix/store/v0x4y2cjbasgmrqsbka2bp8allw8j9ph-darwin-cross-toolchain-nix')
SDK = Path('/nix/store/6zpwyx6p9swad42f0vx6rl4z0dp2kj1g-puredarwin-oss-sdk-26.5/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', required=True, type=Path)
    args = parser.parse_args()
    output = args.output_dir
    if not output.is_absolute():
        parser.error('output directory must be absolute')
    output.mkdir(parents=True, exist_ok=False)
    compiler = TOOLCHAIN / 'bin/arm64-apple-darwin20.4-clang'
    nm = TOOLCHAIN / 'bin/arm64-apple-darwin20.4-nm'
    obj = output / 'darwin_spawn.o'
    sources = {name: sha(HERE / name) for name in ('darwin_spawn.c', 'darwin_spawn.h')}
    command = [str(compiler), '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
               '-O1', '-isystem', str(SDK / 'usr/include'), '-I', str(HERE),
               '-c', str(HERE / 'darwin_spawn.c'), '-o', str(obj)]
    env = dict(os.environ, DARWIN_SDK_ROOT=str(SDK), SDKROOT=str(SDK))
    built = subprocess.run(command, capture_output=True, env=env, timeout=60)
    (output / 'compile.stdout').write_bytes(built.stdout)
    (output / 'compile.stderr').write_bytes(built.stderr)
    if built.returncode:
        raise SystemExit('SDK compile failed; logs preserved')
    imports = subprocess.run([str(nm), '-u', str(obj)], check=True,
                             capture_output=True, text=True, timeout=20).stdout
    (output / 'undefined.txt').write_text(imports)
    assert sources == {name: sha(HERE / name) for name in sources}, 'source changed during build'
    record = {'schema_version': 1, 'status': 'ARM64_RELOCATABLE_BUILD_ONLY',
              'command': command, 'DARWIN_SDK_ROOT': str(SDK),
              'headers': str(SDK / 'usr/include'), 'source_sha256': sources,
              'compiler': str(compiler), 'compiler_sha256': sha(compiler),
              'object': str(obj), 'object_bytes': obj.stat().st_size,
              'object_sha256': sha(obj), 'undefined_symbols': imports.splitlines(),
              'target_execution': False, 'FD_inheritance_proven': False}
    (output / 'build-record.json').write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps({'status': record['status'], 'object_sha256': record['object_sha256']}))


if __name__ == '__main__':
    main()
