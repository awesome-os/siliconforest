#!/usr/bin/env python3
"""Synthetic private writes/lifetimes, compiled mutants, exact SDK objects."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
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
    result = {'scope': 'Host synthetic private table writes and ARM64 objects only',
              'frozen_dependencies': {}, 'host': [], 'mutations': [], 'sdk_objects': [], 'target_executed': False}
    for name in ['m1-gpu-uat-codec', 'm1-gpu-uat-private', 'm1-gpu-rtkit-control']:
        leaf = HERE.parent / name
        receipt = json.loads((leaf / 'source-validation-001.json').read_text())
        for f, digest in receipt['files_sha256'].items():
            if sha(leaf / f) != digest: raise RuntimeError('Frozen predecessor changed: ' + name + '/' + f)
        result['frozen_dependencies'][name] = receipt['source_set_sha256']
    sources = [HERE / 'builder.c', PRIVATE / 'private.c', CODEC / 'uat_codec.c', HERE / 'test_builder.c']
    with tempfile.TemporaryDirectory(prefix='uat-builder-') as temp:
        temp = Path(temp)
        for name, cc, extra in [('asan-ubsan', 'clang', ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']),
                                 ('ndebug', 'cc', ['-DNDEBUG'])]:
            exe = temp / name
            run([cc, '-std=c11', '-Wpedantic', *FLAGS, *extra, *sources, '-o', exe])
            p = run([exe], ENV); result['host'].append({'build': name, 'stdout': p.stdout.strip(), 'stderr': p.stderr})
        source = (HERE / 'builder.c').read_text()
        mutations = [
            ('foreign_allocator_token', 't.owner != c || ', ''),
            ('stale_allocator_token', 's->generation != t.generation', '0', 2),
            ('changed_physical_view', 'v.physical != s->view.physical', '0'),
            ('offset_overrun', 'offset > DUAT_BYTES || length > DUAT_BYTES - offset', 'offset > DUAT_BYTES'),
            ('zero_length', ' || !length || ', ' || '),
            ('source_alias', 'if (overlap(source, length, v.bytes, DUAT_BYTES)) return UATB_SPAN_BAD;', 'if (0) return UATB_SPAN_BAD;'),
            ('permissions', 'p[i].permissions || ', ''),
            ('forward_parent', 'p[i].parent >= i || ', ''),
            ('duplicate_parent_entry', 'p[i].parent == p[j].parent && p[i].entry == p[j].entry', '0'),
            ('zero_only_prefix', 'memset(w->pages[i], 0, DUAT_BYTES)', 'memset(w->pages[i], 0, 64)'),
            ('ignore_plan_mismatch', 'if (memcmp(v.bytes, w->pages[i], DUAT_BYTES)) return UATB_VALIDATE;', '(void)w; if (0) return UATB_VALIDATE;'),
            ('foreign_graph_ticket', 'ticket.owner != b || ticket.generation != b->generation', 'ticket.generation != b->generation'),
            ('stale_graph_ticket', 'ticket.owner != b || ticket.generation != b->generation', 'ticket.owner != b'),
            ('replay_build', 'if (b->phase != UATB_READY) return leave(b, UATB_STATE);', 'if (0) return leave(b, UATB_STATE);'),
            ('retry_uncertain', 'if (uncertain) quarantine_all(b);', '(void)quarantine_all; (void)uncertain;'),
            ('forget_rollback', 'if (duat_shutdown(&b->owner) != DUAT_OK) b->error = UATB_QUARANTINE;',
             'if (0) b->error = UATB_QUARANTINE;', 2),
        ]
        for mutation in mutations:
            name, before, after, *counts = mutation
            count = counts[0] if counts else 1
            if source.count(before) != count: raise RuntimeError('Mutation anchor mismatch: ' + name)
            mutated = temp / (name + '.c'); mutated.write_text(source.replace(before, after))
            exe = temp / name
            run(['clang', '-std=c11', *FLAGS, '-I', HERE, mutated, *sources[1:], '-o', exe])
            p = run([exe], ENV, okay=False)
            if p.returncode != 1 or not re.search(r'test_builder\.c:\d+: ', p.stderr):
                raise RuntimeError('Mutation not detected by failed check: ' + name + ': ' + p.stderr)
            result['mutations'].append({'name': name, 'compiled': True, 'exit': 1, 'failed_check': p.stderr.strip()})
        p = run(['clang', '--analyze', '-std=c11', *FLAGS, HERE / 'builder.c', '-o', temp / 'analysis.plist'])
        if p.stderr: raise RuntimeError(p.stderr)
        result['static_analyzer_diagnostics'] = 0
        xnu = ROOT / 'sources/puredarwin-m1-bringup/src/Kernel/xnu'
        flags = ['-DKERNEL', '-DTARGET_OS_OSX', '-DXNU_PLATFORM_MacOSX=1', '-ffreestanding',
                 '-fno-stack-protector', '-fno-common', '-isystem', HEADERS,
                 '-include', xnu / 'pd_apple_builtins.h']
        defs, imports, deps = set(), set(), {}
        for path in [HERE / 'builder.c', HERE / 'writer_iokit.cpp', PRIVATE / 'private.c',
                     PRIVATE / 'provider_iokit.cpp', CODEC / 'uat_codec.c']:
            obj = temp / (path.stem + '.o'); dep = temp / (path.stem + '.d'); cpp = path.suffix == '.cpp'
            lang = ['-std=c++17', '-fapple-kext', '-fno-exceptions', '-fno-rtti'] if cpp else ['-std=c11']
            p = run([TOOL + ('clang++' if cpp else 'clang'), *lang, *FLAGS, *flags,
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
                                          'undefined': undefined, 'flags': [str(x) for x in lang + FLAGS + flags]})
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
