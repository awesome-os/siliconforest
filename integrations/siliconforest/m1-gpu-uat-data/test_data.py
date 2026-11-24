#!/usr/bin/env python3
"""Typed data ownership and detached leaves, compiled mutants, exact SDK objects."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BUILDER = HERE.parent / 'm1-gpu-uat-builder'
PRIVATE = HERE.parent / 'm1-gpu-uat-private'
CODEC = HERE.parent / 'm1-gpu-uat-codec'
SDK = '/nix/store/6zpwyx6p9swad42f0vx6rl4z0dp2kj1g-puredarwin-oss-sdk-26.5/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk'
TOOL = '/nix/store/v0x4y2cjbasgmrqsbka2bp8allw8j9ph-darwin-cross-toolchain-nix/bin/arm64-apple-darwin20.4-'
HEADERS = '/nix/store/rxi8m8ki35zs9pdj2vnis26wjb1cdfcb-puredarwin-xnu-headers-arm64-0.1/System/Library/Frameworks/Kernel.framework/Versions/A/Headers'
FLAGS = ['-Wall', '-Wextra', '-Werror', '-O1']
ENV = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def run(cmd, env=None, okay=True):
    p = subprocess.run([str(x) for x in cmd], capture_output=True, text=True, timeout=90, env=env)
    if okay and p.returncode: raise RuntimeError(f'{cmd}: {p.returncode}\n{p.stdout}\n{p.stderr}')
    return p
def main():
    result = {'scope': 'Host synthetic private data ownership and scratch-only mappings; ARM64 objects only',
              'frozen_dependencies': {}, 'host': [], 'mutations': [], 'sdk_objects': [], 'target_executed': False}
    for name in ['m1-gpu-uat-codec', 'm1-gpu-uat-private', 'm1-gpu-rtkit-control', 'm1-gpu-uat-builder']:
        leaf = HERE.parent / name
        receipt = json.loads((leaf / 'source-validation-001.json').read_text())
        for f, digest in receipt['files_sha256'].items():
            if sha(leaf / f) != digest: raise RuntimeError('Frozen predecessor changed: ' + name + '/' + f)
        result['frozen_dependencies'][name] = receipt['source_set_sha256']
    reference = ROOT / 'sources/m1n1-baseline/proxyclient/m1n1/hw/uat.py'
    if sha(reference) != '4a4d0300cd3a4ed4fb3de250866993b0d1e00ec3249f93077a871e69c94f0838': raise RuntimeError('Pinned UAT source changed')
    result['MIT_UAT_reference_sha256'] = sha(reference)
    sources = [HERE / 'data.c', BUILDER / 'builder.c', PRIVATE / 'private.c', CODEC / 'uat_codec.c', HERE / 'test_data.c']
    with tempfile.TemporaryDirectory(prefix='uat-data-') as temp:
        temp = Path(temp)
        for name, cc, extra in [('asan-ubsan', 'clang', ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']),
                                 ('ndebug', 'cc', ['-DNDEBUG'])]:
            exe = temp / name
            run([cc, '-std=c11', '-Wpedantic', *FLAGS, *extra, *sources, '-o', exe])
            p = run([exe], ENV); result['host'].append({'build': name, 'stdout': p.stdout.strip(), 'stderr': p.stderr})
        source = (HERE / 'data.c').read_text()
        mutations = [
            ('init_table_alias', 'if (overlap(c, sizeof(*c), v.bytes, DUAT_BYTES)) return UD_ARGUMENT;', 'if (0) return UD_ARGUMENT;'),
            ('foreign_data_token', 't.owner != c || ', ''),
            ('stale_data_token', 's->generation == t.generation', '1'),
            ('stale_graph_ticket', 'ticket.generation == g->generation', '1'),
            ('closed_graph', 'g->phase == UATB_VALID', '1'),
            ('generation_wrap', ' || c->next_generation == UINT64_MAX', ''),
            ('PA36', 'v->physical <= DUAT_PA_END - DUAT_BYTES', '1'),
            ('page_alignment', '!(v->physical % DUAT_BYTES)', '1'),
            ('capacity_exact', 'v->capacity == DUAT_BYTES', '1'),
            ('segment_bound', 'v->segment >= DUAT_BYTES', '1'),
            ('table_PA_alias', 'table.physical == v->physical', '0'),
            ('data_PA_alias', 'other.physical == v->physical', '0'),
            ('table_CPU_alias', 'overlap(table.bytes, DUAT_BYTES, v->bytes, DUAT_BYTES)', '0'),
            ('data_CPU_alias', 'overlap(other.bytes, DUAT_BYTES, v->bytes, DUAT_BYTES)', '0'),
            ('changed_view', '!same(&v, &s->view)', '0'),
            ('zero_readback', 'if (v.bytes[j])', 'if (0)'),
            ('complete_uncertain', 's->state == UD_PREPARED && c->ops.complete(c->provider, s->handle)', 's->state == UD_PREPARED && (c->ops.complete(c->provider, s->handle), 0)'),
            ('allow_execute', 'permissions != UD_READ && permissions != (UD_READ | UD_WRITE)', '(permissions & ~UD_EXEC) != UD_READ && (permissions & ~UD_EXEC) != (UD_READ | UD_WRITE)'),
            ('RO_permission', 'permissions == UD_READ ? UINT64_C(0x40) : 0', 'permissions == UD_READ ? 0 : 0'),
            ('duplicate_VA', 'maps[i].va == maps[j].va', '0'),
            ('duplicate_data', 'maps[i].data.slot == maps[j].data.slot', '0'),
            ('scratch_capacity', 'capacity < sizeof(*w)', 'capacity < sizeof(*w) - 1'),
        ]
        for mutation in mutations:
            name, before, after, *counts = mutation
            count = counts[0] if counts else 1
            if source.count(before) != count: raise RuntimeError('Mutation anchor mismatch: ' + name)
            mutated = temp / (name + '.c'); mutated.write_text(source.replace(before, after))
            exe = temp / name
            run(['clang', '-std=c11', *FLAGS, '-I', HERE, mutated, *sources[1:], '-o', exe])
            p = run([exe], ENV, okay=False)
            if p.returncode != 1 or not re.search(r'test_data\.c:\d+: ', p.stderr):
                raise RuntimeError('Mutation not detected by failed check: ' + name + ': ' + p.stderr)
            result['mutations'].append({'name': name, 'compiled': True, 'exit': 1, 'failed_check': p.stderr.strip()})
        p = run(['clang', '--analyze', '-std=c11', *FLAGS, HERE / 'data.c', '-o', temp / 'analysis.plist'])
        if p.stderr: raise RuntimeError(p.stderr)
        result['static_analyzer_diagnostics'] = 0
        xnu = ROOT / 'sources/puredarwin-m1-bringup/src/Kernel/xnu'
        flags = ['-DKERNEL', '-DTARGET_OS_OSX', '-DXNU_PLATFORM_MacOSX=1', '-ffreestanding',
                 '-fno-stack-protector', '-fno-common', '-isystem', HEADERS,
                 '-include', xnu / 'pd_apple_builtins.h']
        defs, imports, deps = set(), set(), {}
        for number, path in enumerate([HERE / 'data.c', HERE / 'provider_iokit.cpp', BUILDER / 'builder.c',
                     BUILDER / 'writer_iokit.cpp', PRIVATE / 'private.c', PRIVATE / 'provider_iokit.cpp', CODEC / 'uat_codec.c']):
            obj = temp / (str(number) + path.stem + '.o'); dep = temp / (str(number) + path.stem + '.d'); cpp = path.suffix == '.cpp'
            lang = ['-std=c++17', '-fapple-kext', '-fno-exceptions', '-fno-rtti'] if cpp else ['-std=c11']
            p = run([TOOL + ('clang++' if cpp else 'clang'), *lang, *FLAGS, '-Wframe-larger-than=2048', *flags,
                     '-MD', '-MF', dep, '-c', path, '-o', obj], dict(os.environ, DARWIN_SDK_ROOT=SDK))
            header = run([TOOL + 'otool', '-hv', obj]).stdout
            if 'ARM64' not in header or 'OBJECT' not in header: raise RuntimeError(header)
            undefined = run([TOOL + 'nm', '-u', obj]).stdout.split(); imports.update(undefined)
            for line in run([TOOL + 'nm', '-g', obj]).stdout.splitlines():
                fields = line.split()
                if len(fields) == 3 and fields[1] != 'U': defs.add(fields[2])
            for p in dep.read_text().replace('\\\n', ' ').split(':', 1)[1].split():
                deps[str(Path(p).resolve())] = sha(p)
            result['sdk_objects'].append({'source': str(path.relative_to(ROOT)), 'sha256': sha(obj),
                                          'undefined': undefined, 'flags': [str(x) for x in lang + FLAGS + ['-Wframe-larger-than=2048'] + flags]})
        exports = {}
        for name in ['IOKit.exports', 'IOKit.arm64.exports', 'Libkern.exports', 'Mach.exports']:
            for line in (xnu / 'config' / name).read_text().splitlines():
                symbol = line.split(':')[0].strip()
                if symbol.startswith('_'): exports.setdefault(symbol, []).append(name)
        external = imports - defs
        if external - exports.keys(): raise RuntimeError('Uncovered external imports: ' + repr(external))
        result['external_KPI_imports'] = {s: exports[s] for s in sorted(external)}
        result['sdk_dependencies_sha256'] = dict(sorted(deps.items()))
    result['SDK'] = SDK; result['kernel_headers'] = HEADERS; result['compiler'] = TOOL + 'clang'
    print(json.dumps(result, indent=2))
if __name__ == '__main__': main()
