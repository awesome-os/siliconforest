#!/usr/bin/env python3
"""Host faults/mutations and pinned ARM64 SDK objects; never load or execute them."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
CODEC = HERE.parent / 'm1-gpu-uat-codec'
SDK = '/nix/store/6zpwyx6p9swad42f0vx6rl4z0dp2kj1g-puredarwin-oss-sdk-26.5/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk'
TOOL = '/nix/store/v0x4y2cjbasgmrqsbka2bp8allw8j9ph-darwin-cross-toolchain-nix/bin/arm64-apple-darwin20.4-'
HEADERS = '/nix/store/rxi8m8ki35zs9pdj2vnis26wjb1cdfcb-puredarwin-xnu-headers-arm64-0.1/System/Library/Frameworks/Kernel.framework/Versions/A/Headers'
XNU = ROOT / 'sources/puredarwin-m1-bringup/src/Kernel/xnu'
FLAGS = ['-Wall', '-Wextra', '-Werror', '-O1']
SANENV = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')

def run(cmd, env=None, okay=True):
    p = subprocess.run([str(x) for x in cmd], env=env, capture_output=True, text=True, timeout=90)
    if okay and p.returncode:
        raise RuntimeError(f'{cmd}: exit {p.returncode}\n{p.stdout}\n{p.stderr}')
    return p

def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def main():
    result = {'scope': 'Host synthetic ownership tests and ARM64 SDK compile/import audit only',
              'host': [], 'mutations': [], 'sdk_objects': [], 'target_executed': False}
    receipt = json.loads((CODEC / 'source-validation-001.json').read_text())
    for name, digest in receipt['files_sha256'].items():
        if sha(CODEC / name) != digest:
            raise RuntimeError('Frozen codec changed: ' + name)
    result['frozen_codec_source_set'] = receipt['source_set_sha256']
    with tempfile.TemporaryDirectory(prefix='duat-private-') as temp:
        temp = Path(temp)
        sources = [HERE / 'private.c', CODEC / 'uat_codec.c', HERE / 'test_private.c']
        for name, cc, flags in [('asan-ubsan', 'clang', ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']),
                                ('ndebug', 'cc', ['-DNDEBUG'])]:
            exe = temp / name
            run([cc, '-std=c11', '-Wpedantic', *FLAGS, *flags, *sources, '-o', exe])
            p = run([exe], env=SANENV)
            result['host'].append({'build': name, 'stdout': p.stdout.strip(), 'stderr': p.stderr})
        original = (HERE / 'private.c').read_text()
        mutations = [
            ('wrong_owner', 't.owner != c || ', ''),
            ('stale_generation', 's->generation == t.generation', 's->generation != 0'),
            ('context_copy', 'c->self != c', '!c->self'),
            ('reenter', 'if (c->busy) return DUAT_STATE;', 'if (0) return DUAT_STATE;'),
            ('repeat_quarantine_complete', 'if (s->state == DUAT_RETAINED) return DUAT_QUARANTINE;',
             'if (s->state == DUAT_RETAINED) s->state = DUAT_PREPARED;'),
            ('ignore_completion_failure', 'c->ops.complete(c->provider, s->handle))',
             '(c->ops.complete(c->provider, s->handle), 0))'),
            ('pa_width', 'v->physical <= DUAT_PA_END - DUAT_BYTES', 'v->physical <= UINT64_MAX'),
            ('pa_zero', 'v->page_size == DUAT_BYTES && v->physical &&', 'v->page_size == DUAT_BYTES &&'),
            ('pa_alignment', '!(v->physical % DUAT_BYTES)', '1'),
            ('va_alignment', '!((uintptr_t)v->bytes % DUAT_BYTES)', '1'),
            ('length', 'v->length == DUAT_BYTES', '1'),
            ('capacity', 'v->capacity == DUAT_BYTES', '1'),
            ('page_size', 'v->page_size == DUAT_BYTES', '1'),
            ('short_segment', 'v->segment >= DUAT_BYTES', '1'),
            ('duplicate_pa', 'other->view.physical == v.physical', '0'),
            ('duplicate_va', 'overlap(other->view.bytes, DUAT_BYTES, v.bytes, DUAT_BYTES)', '0'),
            ('generation_wrap', '!c->next_generation || c->next_generation == UINT64_MAX', '0'),
            ('skip_codec', 'uat_validate(&snapshot, &summary) != UAT_OK',
             '(uat_validate(&snapshot, &summary), 0)'),
            ('ignore_content_change', 'memcmp(w->pages[n], now.bytes, DUAT_BYTES)', '0'),
            ('ignore_final_generation', 'if (c->slots[i].generation != w->generations[i]) return leave(c, DUAT_STATE);',
             'if (0) return leave(c, DUAT_STATE);'),
        ]
        for name, before, after in mutations:
            if original.count(before) != 1:
                raise RuntimeError('Mutation anchor not unique: ' + name)
            mutated = temp / (name + '.c')
            mutated.write_text(original.replace(before, after))
            exe = temp / name
            run(['clang', '-std=c11', *FLAGS, '-I', HERE, mutated, *sources[1:], '-o', exe])
            p = run([exe], env=SANENV, okay=False)
            if p.returncode != 1 or not re.search(r'test_private\.c:\d+: ', p.stderr):
                raise RuntimeError('Mutation survived or lacked failed-check evidence: ' + name)
            result['mutations'].append({'name': name, 'compiled': True, 'failed_checks': True,
                                        'exit_code': p.returncode, 'failure': p.stderr.strip()})
        p = run(['clang', '--analyze', '-std=c11', *FLAGS, HERE / 'private.c',
                 '-o', temp / 'analysis.plist'])
        if p.stderr:
            raise RuntimeError('Static analyzer diagnostics: ' + p.stderr)
        result['static_analyzer_diagnostics'] = 0
        env = dict(os.environ, DARWIN_SDK_ROOT=SDK)
        kernel_flags = ['-DKERNEL', '-DTARGET_OS_OSX', '-DXNU_PLATFORM_MacOSX=1',
                        '-ffreestanding', '-fno-stack-protector', '-fno-common',
                        '-isystem', HEADERS, '-include', XNU / 'pd_apple_builtins.h']
        exports = {}
        for name in ['IOKit.exports', 'IOKit.arm64.exports', 'Libkern.exports', 'Mach.exports']:
            for line in (XNU / 'config' / name).read_text().splitlines():
                symbol = line.split(':')[0].strip()
                if symbol.startswith('_'):
                    exports.setdefault(symbol, []).append(name)
        imports = set()
        definitions = set()
        dependencies = {}
        for source in [HERE / 'private.c', HERE / 'provider_iokit.cpp', CODEC / 'uat_codec.c']:
            obj = temp / (source.stem + '.o')
            dep = temp / (source.stem + '.d')
            cpp = source.suffix == '.cpp'
            lang = ['-std=c++17', '-fapple-kext', '-fno-exceptions', '-fno-rtti'] if cpp else ['-std=c11']
            cmd = [TOOL + ('clang++' if cpp else 'clang'), *lang, *FLAGS, *kernel_flags,
                   '-MD', '-MF', dep, '-c', source, '-o', obj]
            p = run(cmd, env)
            header = run([TOOL + 'otool', '-hv', obj]).stdout
            if 'ARM64' not in header or 'OBJECT' not in header:
                raise RuntimeError(header)
            undefined = run([TOOL + 'nm', '-u', obj]).stdout.split()
            imports.update(undefined)
            for line in run([TOOL + 'nm', '-g', obj]).stdout.splitlines():
                fields = line.split()
                if len(fields) == 3 and fields[1] != 'U':
                    definitions.add(fields[2])
            deptext = dep.read_text().replace('\\\n', ' ')
            for path in deptext.split(':', 1)[1].split():
                path = str(Path(path).resolve())
                dependencies[path] = sha(path)
            result['sdk_objects'].append({'source': str(source.relative_to(ROOT)),
                                          'sha256': sha(obj), 'undefined': undefined, 'stderr': p.stderr,
                                          'compile_flags': [str(x) for x in [*lang, *FLAGS, *kernel_flags]]})
        external = imports - definitions
        missing = external - exports.keys()
        if missing:
            raise RuntimeError('Imports absent from inspected KPI exports: ' + repr(missing))
        result['external_import_kpi_audit'] = {s: exports[s] for s in sorted(external)}
        result['virtual_calls_limit'] = 'IOBMD inspect/prepare/complete/release dispatch through vtables; their declarations and source exports were reviewed separately. Object import coverage is not runtime ABI/KC proof.'
        result['sdk_dependency_sha256'] = dict(sorted(dependencies.items()))
    result['SDK'] = SDK
    result['kernel_headers'] = HEADERS
    result['compiler'] = TOOL + 'clang'
    result['host_compiler'] = run(['clang', '--version']).stdout.splitlines()[0]
    print(json.dumps(result, indent=2))

if __name__ == '__main__':
    main()
