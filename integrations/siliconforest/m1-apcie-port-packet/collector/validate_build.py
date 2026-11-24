#!/usr/bin/env python3
"""Read-only actual Mach-O/source/header/import checks. No target execution."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import struct
import subprocess

HERE = Path(__file__).resolve().parent
SYSTEM = Path('/nix/store/9i1vaph8rfh102h1x4v7gj7fl7q4x7rm-puredarwin-libsystem-spawnattr-0.1/usr/lib/libSystem.B.dylib')
DYLD = Path('/nix/store/jll17vb2bl849knd0b68n358c53wh1gl-puredarwin-libsystem-0.1/usr/lib/system/libdyld.dylib')
TINY = Path('/nix/store/iy73f9hyr3hcq2n1yhmbsyx2pkmblw09-puredarwin-tiny-iokit-mig-header-0.1')
PINS = {
    SYSTEM: '9fc03c87e929310d881bd5b440bf0f3d7f0b230d79521b07cea539cf5c7e481e',
    DYLD: '428e224ec2621bf3a2a7859402603a9a0fd6abe8b3e26c38edeb869e0ce58a0d',
    TINY / 'include/device_user.h': '2315eb7aaa9395eab01777f8bf5b22b6c1ae653492300d9ffa8fd18b8f413795',
    TINY / 'lib/libIOKit.a': 'c099fba1a5376cffd8d907e82aff4d6ee077764ac4eb144c45288da5948e6491',
}


def ref(path):
    data = path.read_bytes()
    return {'path': str(path), 'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)}


def command(*args):
    return subprocess.check_output(args, text=True)


def exports(path):
    return {line.split()[1] for line in command('llvm-objdump', '--macho', '--exports-trie', str(path)).splitlines()
            if re.match(r'^0x[0-9a-fA-F]+\s+\S+', line)}


def imports(path):
    rows = []
    for line in command('llvm-nm', '-m', '-u', str(path)).splitlines():
        match = re.fullmatch(r'\s*\(undefined\) (?:\[.*?\] )?(weak )?external (\S+) \((.*?)\)', line)
        if not match:
            raise ValueError('unclassified undefined record: ' + line)
        rows.append(dict(symbol=match[2], weak=bool(match[1]), origin=match[3]))
    return rows


def validate(output):
    for path, sha in PINS.items():
        if ref(path)['sha256'] != sha:
            raise ValueError('pinned bytes differ: ' + str(path))
    binary = output / 'usr/bin/darwin-apcie-port-observe'
    raw = binary.read_bytes()
    header = struct.unpack_from('<8I', raw)
    if header[:4] != (0xfeedfacf, 0x100000c, 0, 2):
        raise ValueError('not ARM64_ALL MH_EXECUTE')
    command_count, command_size = header[4:6]
    at, dependencies, interpreter, versions, signed = 32, [], None, [], False
    for _ in range(command_count):
        kind, size = struct.unpack_from('<II', raw, at)
        if size < 8 or at + size > 32 + command_size:
            raise ValueError('load commands')
        if kind in (0xc, 0xe):
            offset = struct.unpack_from('<I', raw, at + 8)[0]
            value = raw[at + offset:at + size].split(b'\0')[0].decode('ascii')
            if kind == 0xc:
                dependencies.append(value)
            else:
                interpreter = value
        elif kind == 0x32:
            versions.append(struct.unpack_from('<III', raw, at + 8))
        elif kind == 0x1d:
            signed = True
        at += size
    if at != 32 + command_size or dependencies != ['/usr/lib/libSystem.B.dylib'] or interpreter != '/usr/lib/dyld' or versions != [(1, 0x1a0500, 0x1a0500)] or not signed:
        raise ValueError('target ABI/dependency contract')
    available = exports(SYSTEM) | exports(DYLD)
    imp = imports(binary)
    if not imp or any(row['origin'] != 'from libSystem' or row['symbol'] not in available for row in imp):
        raise ValueError('actual retained-library import closure')
    system_imports = imports(SYSTEM)
    ordinal = [row for row in system_imports if row['origin'] == 'from libdyld']
    if len(ordinal) != 49 or any(row['symbol'] not in exports(DYLD) for row in ordinal):
        raise ValueError('libSystem actual libdyld ordinal closure')
    if any(row['origin'] not in ('from libdyld', 'dynamically looked up') for row in system_imports):
        raise ValueError('unknown libSystem import origin')
    # The older qualified 9fc/428 closure distinguishes Objective-C flat/dynamic
    # records from its 49 actual libdyld ordinal records. Do not claim objc load.
    text_symbols = sorted(line.split()[-1] for line in command('llvm-nm', str(binary)).splitlines()
                          if re.search(r' [Tt] ', line))
    expected = {'_IOMasterPort', '_IOObjectRelease', '_IOKitEnsureTaskSelf',
                '_io_registry_entry_from_path', '_io_registry_entry_get_property_bytes',
                '___MIG_check__Reply__io_registry_entry_from_path_t',
                '___MIG_check__Reply__io_registry_entry_get_property_bytes_t',
                '__mh_execute_header', '_main', '_fetch', '_apc_run', '_apc_validate', '_finish', '_emit_raw'}
    if set(text_symbols) != expected:
        raise ValueError('unexpected linked text/reachable API surface')
    build = output / 'share/apcie-collector-build'
    copies = {}
    for name in ('collector.c', 'collector.h', 'main_mig.c'):
        copy = build / 'source' / name
        if copy.read_bytes() != (HERE / name).read_bytes():
            raise ValueError('compiled source copy mismatch')
        copies[name] = ref(copy)
    dependencies_files = sorted((build / 'compiler-dependencies').glob('*.d'))
    if [p.name for p in dependencies_files] != ['collector.d', 'main_mig.d']:
        raise ValueError('missing TU dependency records')
    actual_inputs = set()
    for path in dependencies_files:
        text = path.read_text().replace('\\\n', ' ')
        tokens = shlex.split(text.split(':', 1)[1])
        actual_inputs.update(tokens)
    actual_headers = {path: ref(Path(path)) for path in sorted(actual_inputs)}
    if str(TINY / 'include/device_user.h') not in actual_headers:
        raise ValueError('actual generated MIG header absent')
    previous = HERE.parents[3] / 'reports/sprint-010/native-libc-resolver-attempt002-runtime-closure-addendum.json'
    return dict(schema_version=1, status='SDK_ARM64_SOURCE_AND_IMPORTS_VERIFIED_NOT_EXECUTED',
                binary=ref(binary), platform='macOS26.5', CPU='ARM64_ALL', interpreter=interpreter,
                dependencies=dependencies, code_signature_load_command=True,
                imports=imp, import_count=len(imp), unresolved=[], text_symbols=text_symbols,
                source_copy=copies, compiler=(build / 'compiler.txt').read_text(),
                actual_compiler_inputs=actual_headers,
                build_evidence=[ref(p) for p in dependencies_files] + [ref(build / n) for n in ('header.txt', 'dependencies.txt', 'imports.txt', 'compiler.txt', 'libIOKit.a')],
                retained_libraries=[ref(SYSTEM), ref(DYLD)], tiny_IOKit=[ref(p) for p in PINS if p not in (SYSTEM, DYLD)],
                libSystem_actual_libdyld_ordinal_imports=ordinal,
                libSystem_dynamic_lookup_record_count=sum(row['origin'] == 'dynamically looked up' for row in system_imports),
                qualified_retained_library_provenance=ref(previous),
                no_open_connect_setproperty_text=True,
                limits=['No Darwin/M1 execution', 'No registry property was copied on target',
                        'No PCIe/WiFi readiness or chip identity claim',
                        'No hardware/deployment/KC/packet assembly',
                        'Raw MIG returns bytes, not an independently queried OSData type'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    print(json.dumps(validate(args.output), indent=2, sort_keys=True))
