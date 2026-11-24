#!/usr/bin/env python3
"""Host-only transcript, fault/mutation tests and ARM64 object compilation."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SDK = '/nix/store/6zpwyx6p9swad42f0vx6rl4z0dp2kj1g-puredarwin-oss-sdk-26.5/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk'
TOOL = '/nix/store/v0x4y2cjbasgmrqsbka2bp8allw8j9ph-darwin-cross-toolchain-nix/bin/arm64-apple-darwin20.4-'
HEADERS = '/nix/store/rxi8m8ki35zs9pdj2vnis26wjb1cdfcb-puredarwin-xnu-headers-arm64-0.1/System/Library/Frameworks/Kernel.framework/Versions/A/Headers'
FLAGS = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-O1']
ENV = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
GOOD = '''INIT 1 1000000
TX 2
COMMIT 3 1 WOULD_BLOCK
TX 4
COMMIT 5 1 ACCEPTED
RX 6 0 00100000000c000b
TX 7
COMMIT 8 2 ACCEPTED
RX 9 0 008000000000011f
TX 10
COMMIT 11 3 ACCEPTED
RX 12 0 0088000100000003
TX 13
COMMIT 14 4 ACCEPTED
END 15
'''
GOOD_OUTPUT = '''TX sequence=1 endpoint=0 word=0060000000000220
TX sequence=1 endpoint=0 word=0060000000000220
TX sequence=2 endpoint=0 word=00200000000c000c
TX sequence=3 endpoint=0 word=0080000000000001
TX sequence=4 endpoint=0 word=0088000100000000
CONTROL_DISCOVERED_PROTOCOL_ONLY version=12 accepted_tx=4 received=3 system_endpoints=0000011e
'''

def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def run(cmd, env=None, data=None, okay=True):
    p = subprocess.run([str(x) for x in cmd], env=env, input=data, capture_output=True, text=True, timeout=60)
    if okay and p.returncode:
        raise RuntimeError(f'{cmd}: exit {p.returncode}\n{p.stdout}\n{p.stderr}')
    return p

def main():
    result = {'scope': 'Synthetic RTKit protocol only; no hardware mailbox', 'host': [], 'mutations': [],
              'transcripts': [], 'target_executed': False}
    report = ROOT / 'reports/sprint-010/gpu-context-firmware-ownership-source-001.json'
    facts = json.loads(report.read_text())['provenance']
    ref = ROOT / facts['source']
    for name, digest in facts['selected_files_sha256'].items():
        if sha(ref / name) != digest:
            raise RuntimeError('Pinned reference changed: ' + name)
        if (ref / name).read_bytes() != subprocess.check_output(['git', '-C', str(ref), 'show', facts['pin'] + ':' + name]):
            raise RuntimeError('Pinned Git object mismatch: ' + name)
    result['reference_report_sha256'] = sha(report)
    result['pinned_MIT_files_reverified'] = len(facts['selected_files_sha256'])
    result['additional_MIT_buffer_layout_sources_sha256'] = {}
    for name in ['crash.py', 'syslog.py', 'ioreporting.py', 'oslog.py']:
        rel = 'proxyclient/m1n1/fw/asc/' + name
        if (ref / rel).read_bytes() != subprocess.check_output(['git', '-C', str(ref), 'show', facts['pin'] + ':' + rel]):
            raise RuntimeError('Buffer layout reference differs from pin: ' + rel)
        result['additional_MIT_buffer_layout_sources_sha256'][rel] = sha(ref / rel)
    result['frozen_dependencies'] = {}
    for name in ['m1-gpu-uat-codec', 'm1-gpu-uat-private']:
        leaf = HERE.parent / name
        receipt = json.loads((leaf / 'source-validation-001.json').read_text())
        for file, digest in receipt['files_sha256'].items():
            if sha(leaf / file) != digest:
                raise RuntimeError('Frozen dependency changed: ' + name + '/' + file)
        result['frozen_dependencies'][name] = receipt['source_set_sha256']
    with tempfile.TemporaryDirectory(prefix='rtkit-control-') as temp:
        temp = Path(temp)
        for name, cc, flags in [('asan-ubsan', 'clang', ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']),
                                ('ndebug', 'cc', ['-DNDEBUG'])]:
            exe = temp / name
            run([cc, *FLAGS, *flags, HERE / 'control.c', HERE / 'test_control.c', '-o', exe])
            p = run([exe], env=ENV)
            result['host'].append({'build': name, 'stdout': p.stdout.strip(), 'stderr': p.stderr})
        transcript = temp / 'transcript'
        run(['clang', *FLAGS, '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
             HERE / 'control.c', HERE / 'transcript.c', '-o', transcript])
        p = run([transcript], env=ENV, data=GOOD)
        if p.stdout != GOOD_OUTPUT or p.stderr:
            raise RuntimeError('Golden transcript output mismatch: ' + p.stdout + p.stderr)
        result['transcripts'].append({'name': 'golden_v12_backpressure', 'exit': 0, 'exact_output_match': True})
        v11 = GOOD.replace('00100000000c000b', '00100000000b000b')
        p = run([transcript], env=ENV, data=v11)
        if p.stdout != GOOD_OUTPUT.replace('00200000000c000c', '00200000000b000b').replace('version=12', 'version=11'):
            raise RuntimeError('Version11 transcript mismatch')
        result['transcripts'].append({'name': 'golden_v11', 'exit': 0, 'exact_output_match': True})
        negatives = {
            'missing_final_commit': GOOD.replace('COMMIT 14 4 ACCEPTED\n', ''),
            'missing_hello_commit': GOOD.replace('COMMIT 8 2 ACCEPTED\n', ''),
            'early_hello': GOOD.replace('COMMIT 5 1 ACCEPTED\n', ''),
            'missing_gpu_endpoint': GOOD.replace('0088000100000003', '0088000100000001'),
            'missing_last': GOOD.replace('0088000100000003', '0080000100000003'),
            'duplicate_map': GOOD.replace('0088000100000003', '008800000000011f'),
            'stale_ticket': GOOD.replace('COMMIT 14 4', 'COMMIT 14 3'),
            'unsupported_version': GOOD.replace('00100000000c000b', '00100000000d000d'),
            'bad_minmax': GOOD.replace('00100000000c000b', '00100000000b000c'),
            'timeout': GOOD.replace('END 15', 'END 1000001'),
            'rollback': GOOD.replace('END 15', 'END 1'),
            'zero_clock': GOOD.replace('RX 6', 'RX 0'),
            'overflow_deadline': GOOD.replace('INIT 1 1000000', 'INIT 18446744073709551615 1'),
            'uncertain_delivery': GOOD.replace('COMMIT 14 4 ACCEPTED', 'COMMIT 14 4 UNCERTAIN'),
            'failed_delivery': GOOD.replace('COMMIT 14 4 ACCEPTED', 'COMMIT 14 4 FAILED'),
            'actual_system_buffer': GOOD.replace('RX 12 0 0088000100000003', 'RX 12 2 0010200001234000'),
            'ambiguous_44bit_buffer': GOOD.replace('RX 12 0 0088000100000003', 'RX 12 2 0010240000000000'),
            'ambiguous_oslog': GOOD.replace('RX 12 0 0088000100000003', 'RX 12 8 0102000000000000'),
            'postterminal_packet': GOOD.replace('END 15', 'RX 15 0 0088000100000003\nEND 16'),
            'unexpected_power_ack': GOOD.replace('RX 12 0 0088000100000003', 'RX 12 0 0070000000000020'),
            'reserved_bit': GOOD.replace('00100000000c000b', '80100000000c000b'),
            'endpoint_out_of_range': GOOD.replace('RX 6 0', 'RX 6 256'),
            'trailing_after_end': GOOD + 'POLL 16\n',
            'no_end': GOOD.replace('END 15\n', ''),
            'no_final_newline': GOOD.rstrip('\n'),
            'extra_field': GOOD.replace('TX 2\n', 'TX 2 junk\n'),
            'negative_number': GOOD.replace('TX 2\n', 'TX -2\n'),
            'overflow_word': GOOD.replace('00100000000c000b', '100100000000c000b00'),
            'long_line': 'x' * 250 + '\n',
            'embedded_nul': GOOD.replace('TX 2\n', 'TX 2\0\n'),
            'unknown_command': GOOD.replace('TX 2\n', 'MMIO 2\n'),
            'empty': '',
            'peek_budget': 'INIT 1 1000000\n' + 'TX 2\n' * 128 + 'END 3\n',
        }
        for name, text in negatives.items():
            p = run([transcript], env=ENV, data=text, okay=False)
            if p.returncode not in (1, 2) or 'CONTROL_DISCOVERED_PROTOCOL_ONLY' in p.stdout:
                raise RuntimeError('Invalid transcript accepted: ' + name)
            if name in ('actual_system_buffer', 'ambiguous_44bit_buffer', 'ambiguous_oslog') and 'UNRESOLVED_BUFFER' not in p.stderr:
                raise RuntimeError('Buffer evidence omitted: ' + name)
            result['transcripts'].append({'name': name, 'exit': p.returncode, 'failure': p.stderr.strip()})
        source = (HERE / 'control.c').read_text()
        mutations = [
            ('wrong_owner', 't.owner != c || ', ''),
            ('stale_ticket', 't.sequence != c->pending.ticket.sequence', '0'),
            ('skip_offer', ' || !c->offered', ''),
            ('backpressure_advances', 'if (delivery == RTC_WOULD_BLOCK) return RTC_OK;', 'if (delivery == RTC_WOULD_BLOCK) delivery = RTC_ACCEPTED;'),
            ('uncertain_accepts', 'if (delivery == RTC_DELIVERY_UNCERTAIN) return fail(c, RTC_SEND_UNCERTAIN);', 'if (delivery == RTC_DELIVERY_UNCERTAIN) delivery = RTC_ACCEPTED;'),
            ('clock_zero', '!now || now < c->last_ns', 'now < c->last_ns && now != 0'),
            ('clock_rollback', '!now || now < c->last_ns', '!now'),
            ('deadline_equality', 'now >= c->deadline_ns', 'now > c->deadline_ns'),
            ('step_budget', 'c->result.steps >= RTC_STEP_LIMIT', 'c->result.steps > RTC_STEP_LIMIT'),
            ('deadline_overflow', ' || timeout > UINT64_MAX - now', ''),
            ('reversed_version', 'lo > hi || ', ''),
            ('missing_gpu_endpoint', '(app & 3) != 3', '(app & 3) == 0'),
            ('duplicate_map', 'c->seen_bases & (1u << base)', '0'),
            ('unknown_system_endpoint', '!base && (bitmap & ~KNOWN_SYSTEM)', '0'),
            ('hello_reserved', '(word & ~HELLO_MASK)', '0'),
            ('map_reserved', '(word & ~MAP_MASK)', '0'),
            ('wrap_ticket', '!c->next_sequence || c->next_sequence == UINT64_MAX', '0'),
            ('queued_rx_bypass', 'if (queued(c->result.phase)) return fail(c, RTC_STATE);',
             'if (c->result.phase == RTC_MAP_ACK_QUEUED) c->result.phase = RTC_WAIT_MAP;'),
            ('last_not_required', 'c->staged_last ? RTC_CONTROL_DISCOVERED_PROTOCOL_ONLY : RTC_WAIT_MAP',
             'RTC_CONTROL_DISCOVERED_PROTOCOL_ONLY'),
            ('lose_buffer_address', 'word & ((UINT64_C(1) << 42) - 1)', '0'),
            ('hide_buffer_request', 'b->outstanding = 1;', 'b->outstanding = 0;'),
            ('commit_version_early', 'c->candidate_version = hi < 12 ? hi : 12;',
             'c->candidate_version = hi < 12 ? hi : 12; c->result.negotiated_version = c->candidate_version;'),
        ]
        for name, before, after in mutations:
            if source.count(before) != 1:
                raise RuntimeError('Mutation anchor not unique: ' + name)
            mutated = temp / (name + '.c'); mutated.write_text(source.replace(before, after))
            exe = temp / name
            run(['clang', *FLAGS, '-I', HERE, mutated, HERE / 'test_control.c', '-o', exe])
            p = run([exe], env=ENV, okay=False)
            if p.returncode != 1 or not re.search(r'test_control\.c:\d+: ', p.stderr):
                raise RuntimeError('Mutant not detected by check: ' + name)
            result['mutations'].append({'name': name, 'compiled': True, 'failed_check': p.stderr.strip(), 'exit': 1})
        p = run(['clang', '--analyze', *FLAGS, HERE / 'control.c', '-o', temp / 'core.plist'])
        if p.stderr: raise RuntimeError(p.stderr)
        p = run(['clang', '--analyze', *FLAGS, HERE / 'transcript.c', '-o', temp / 'transcript.plist'])
        if p.stderr: raise RuntimeError(p.stderr)
        result['static_analyzer_diagnostics'] = 0
        obj = temp / 'control.o'; dep = temp / 'control.d'
        kernel_flags = ['-DKERNEL', '-DTARGET_OS_OSX', '-DXNU_PLATFORM_MacOSX=1', '-ffreestanding',
                        '-fno-stack-protector', '-fno-common', '-isystem', HEADERS]
        run([TOOL + 'clang', *FLAGS, *kernel_flags, '-MD', '-MF', dep, '-c', HERE / 'control.c', '-o', obj],
            env=dict(os.environ, DARWIN_SDK_ROOT=SDK))
        header = run([TOOL + 'otool', '-hv', obj]).stdout
        if 'ARM64' not in header or 'OBJECT' not in header: raise RuntimeError(header)
        undefined = run([TOOL + 'nm', '-u', obj]).stdout.split()
        exports = {}
        xnu = ROOT / 'sources/puredarwin-m1-bringup/src/Kernel/xnu'
        for name in ['IOKit.exports', 'IOKit.arm64.exports', 'Libkern.exports', 'Mach.exports']:
            for line in (xnu / 'config' / name).read_text().splitlines():
                symbol = line.split(':')[0].strip()
                if symbol.startswith('_'): exports.setdefault(symbol, []).append(name)
        if set(undefined) - exports.keys(): raise RuntimeError('Uncovered KPI import: ' + repr(undefined))
        result['ARM64_object'] = {'sha256': sha(obj), 'undefined_KPI_audit': {s: exports[s] for s in undefined},
                                  'SDK': SDK, 'kernel_headers': HEADERS, 'compiler': TOOL + 'clang',
                                  'flags': FLAGS + kernel_flags, 'executed': False}
        deps = dep.read_text().replace('\\\n', ' ').split(':', 1)[1].split()
        result['sdk_dependencies_sha256'] = {str(Path(p).resolve()): sha(p) for p in sorted(set(deps))}
    print(json.dumps(result, indent=2))

if __name__ == '__main__':
    main()
