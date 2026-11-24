#!/usr/bin/env python3
"""Host-only checks for darwin-dart-survey (no SDK, IOKit, MMIO or hardware).

Builds the pure core with the reviewed sampler under ASan/UBSan, decodes the
emitted records independently, rejects compiled mutants, refuses NDEBUG and
checks the Darwin wrapper's source shape. Temporary files live outside the tree.
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
FLAGS = ['-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-O1', '-g',
         '-fno-omit-frame-pointer', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']
ENV = dict(os.environ, ASAN_OPTIONS='detect_leaks=0:abort_on_error=1',
           UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
NONCE = '0123456789abcdef0123456789abcdef0123456789abcdef'
MUTANTS = {
    'oversize_accepted': ('n != DLC_DIAG_BYTES ||', 'n < DLC_DIAG_BYTES ||'),
    'tail_half_checked': (': (so || sv)) return -1;', ': (so && sv)) return -1;'),
    'sampled_not_exact': ('if (s != SAMPLES || reads != FULL_READS ||', 'if ('),
    'refused_as_success': ('code = DLC_EXIT_REFUSED;', 'code = DLC_EXIT_SAMPLED;'),
    'missing_as_refused': ('int code = DLC_EXIT_FAIL,', 'int code = DLC_EXIT_REFUSED,'),
    'instance_mismatch_ignored': ('if (strcmp(inst, "absent") && strcmp(inst, "equal")) verdict',
                                  'if (0) verdict'),
    'unknown_status': ('if (result < -4 || result > 2) return -1;', ''),
    'guard_position_unchecked': ('region == guard_region[step - 1] && off == guard_offset[step - 1]', '1'),
    'final_guard_unchecked': ('if (!guard_passes(i, guard_word(b, i))) return -1;', '(void)i;'),
    'not_ready_value_unchecked': ('if (!guard_at || guard_passes(step - 1, guard_word(b, step - 1))) return -1;',
                                  'if (!guard_at) return -1;'),
    'limit_accepted': ('if (result == -4) return -1;', ''),
    'prefix_shift_unchecked': ('if (s >= 1 && ((le32(samples + 4) >> 24) & 15) != 14 && stop_unsupported != 1) return -1;', ''),
    'prefix_lock_unchecked': ('if (s >= 3 && (le32(samples + 16 + 4) & 0x8000u) && stop_unsupported != 3) return -1;', ''),
    'prefix_streams_unchecked': ('if (s >= 4 && (le32(samples + 24 + 4) & 0xffff0000u) && stop_unsupported != 4) return -1;', ''),
}


def build(tmp, core_text, name):
    core = tmp / f'{name}.c'
    core.write_text(core_text)
    exe = tmp / name
    r = subprocess.run(['cc', *FLAGS, '-I', str(HERE), str(HERE / 'test_collector.c'), str(core),
                        str(HERE.parent / 'survey.c'), '-lcrypto', '-o', str(exe)],
                       capture_output=True, text=True)
    if r.returncode:  # a compiler failure is never counted as a mutant kill
        raise RuntimeError(f'{name} did not compile:\n' + r.stderr[-3000:])
    return exe


def decode(text, nonce):
    """Independent decoder: every line nonce-bound; contiguous 64-byte chunks; digest."""
    records, verdicts = {}, []
    for line in text.splitlines():
        parts = line.split(' ')
        assert parts[1] == nonce, line
        if parts[0] == 'DL13_DART_RECORD_BEGIN':
            assert parts[4] == 'chunk_bytes=64' and parts[2] not in records
            records[parts[2]] = {'bytes': int(parts[3][6:]), 'data': b'', 'next': 0}
        elif parts[0] == 'DL13_DART_RECORD_HEX':
            r = records[parts[2]]
            assert parts[3] == f"index={r['next']}" and len(parts) == 5
            chunk = bytes.fromhex(parts[4])
            assert 0 < len(chunk) <= 64 and (len(chunk) == 64 or len(r['data']) + len(chunk) == r['bytes'])
            r['data'] += chunk
            r['next'] += 1
        elif parts[0] == 'DL13_DART_RECORD_END':
            r = records[parts[2]]
            fields = dict(p.split('=') for p in parts[3:])
            assert int(fields['bytes']) == r['bytes'] == len(r['data'])
            assert int(fields['chunks']) == r['next'] == (r['bytes'] + 63) // 64
            assert fields['sha256'] == hashlib.sha256(r['data']).hexdigest()
            r['ended'] = True
        elif parts[0] == 'DL13_DART_VERDICT':
            assert line.endswith('initialization_proven=false')
            verdicts.append(dict(p.split('=') for p in parts[2:]))
        elif parts[0] != 'DL13_DART_COPY':
            raise AssertionError('unexpected line ' + line)
    assert len(verdicts) == 1 and all(r.get('ended') for r in records.values())
    return records, verdicts[0]


def main():
    out = {}
    with tempfile.TemporaryDirectory(prefix='dart-collector-host-') as t:
        tmp = Path(t)
        core = (HERE / 'dart_collector.c').read_text()
        exe = build(tmp, core, 'harness')
        run = subprocess.run([str(exe), str(tmp)], env=ENV, capture_output=True, text=True)
        assert run.returncode == 0, run.stderr[-4000:]
        summary = run.stdout.strip().splitlines()[-1]
        assert summary.startswith('HOST_DART_COLLECTOR groups=6 ')
        out['harness'] = summary
        for name, verdict in (('sampled', 'SAMPLED'), ('refused', 'REFUSED'), ('malformed', 'MALFORMED')):
            records, v = decode((tmp / f'{name}.txt').read_text(), NONCE)
            assert v['verdict'] == verdict
            if name != 'malformed':
                raw = (tmp / f'{name}.bin').read_bytes()
                assert records['PDDARTSurveyDiagnosticV1']['data'] == raw and len(raw) == 764
        assert 'PDDARTSurveyObservationV1' in decode((tmp / 'sampled.txt').read_text(), NONCE)[0]
        out['decoded_formats'] = ['sampled', 'refused', 'malformed']
        guard = subprocess.run(['cc', '-std=c11', '-DNDEBUG', '-E', '-I', str(HERE),
                                str(HERE / 'test_collector.c')], capture_output=True, text=True)
        assert guard.returncode != 0 and '#error DLC_TEST_REQUIRES_ASSERTIONS_NDEBUG_FORBIDDEN' in guard.stderr
        out['ndebug_refused'] = True
        killed = []
        for name, (old, new) in MUTANTS.items():
            assert core.count(old) == 1, name
            mexe = build(tmp, core.replace(old, new), 'mutant_' + name)
            r = subprocess.run([str(mexe)], env=ENV, capture_output=True, text=True)
            assert r.returncode != 0, 'mutant survived: ' + name
            # Only a semantic assertion failure counts; sanitizer aborts do not.
            assert 'Sanitizer' not in r.stderr and ('Assertion' in r.stderr or 'expected' in r.stderr), name
            killed.append(name)
        out['mutants_killed'] = killed
    wrapper = (HERE / 'collector_main.c').read_text()
    calls = set(re.findall(r'\b(IO\w+|io_\w+|host_\w+|mach_\w+|vm_\w+|mmap)\s*\(', wrapper))
    assert calls == {'IOMasterPort', 'io_registry_entry_from_path', 'io_registry_entry_get_property_bytes',
                     'IOObjectRelease'}, calls
    for forbidden in ('IOServiceOpen', 'IOConnect', 'mmap', 'vm_map', '/dev/', 'open(', 'while', 'for ('):
        assert forbidden not in wrapper, forbidden
    out['wrapper_calls'] = sorted(calls)
    out['status'] = 'HOST_COLLECTOR_CHECKS_PASS'
    print(json.dumps(out, sort_keys=True))


if __name__ == '__main__':
    if not shutil.which('cc'):
        sys.exit('cc required')
    main()
