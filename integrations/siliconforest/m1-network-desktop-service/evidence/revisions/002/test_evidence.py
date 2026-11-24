#!/usr/bin/env python3
"""Synthetic evidence corpus. Imports fixture data generator only, never C proof."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import unittest
sys.dont_write_bytecode = True
import qualify as q

NONCE = '0123456789abcdef' * 3
FIXTURE = q.BASE / 'desktop_proof/check.py'


def fixture_records():
    # Shared producer-shaped golden data; parser/oracle code is independent.
    if q.digest(FIXTURE.read_bytes()) != 'cfdee4a5804fd838ea26eb9b1112766e88bb7c80ece4b34f6ef0590fe1b58038':
        raise RuntimeError('Frozen fixture source changed')
    spec = importlib.util.spec_from_file_location('desktop_fixture_data_only', FIXTURE)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    files = module.records()  # Pure data constructor only; main/check never called.
    result = {'session_result': files['session-result.txt'], 'manager_status': files['manager-status.log']}
    mapping = dict(result='result.json', child_ready='child-ready.json', child='child.json', client='network-client.json',
                   client_pid='network-client.pid', shell_return='network-shell-return.txt', pty_rx='pty-rx.bin',
                   pty_tx='pty-tx.bin', grid='grid-final.txt', stdout='application-stdout.log', stderr='application-stderr.log')
    for slot in (1, 2):
        prefix = f'{slot:04d}' + NONCE[:44] + '/'
        for key, filename in mapping.items():
            result[f'app{slot}_{key}'] = files[prefix + filename]
    return result


def chunks(kind, raw, nonce=NONCE):
    return [f'DL30_OBSERVE_{nonce} kind={kind} total={len(raw)} offset={at} hex={raw[at:at + 64].hex()}\n'.encode()
            for at in range(0, len(raw), 64)] or [f'DL30_OBSERVE_{nonce} kind={kind} total=0 offset=0 hex=\n'.encode()]


def console(records=None, extra=None):
    records = records or fixture_records()
    front = (b'kind=2 pid=90 created=1 reaped=1 lost=0 error=0 wait_status=0 output_bytes=0 stderr_bytes=0 '
             b'output_eof=1 stderr_eof=1 term_sent=0 kill_sent=0 stop_requested=0 started_ms=100 last_ms=100000')
    extras = extra if extra is not None else [
        ('frontend_child', front), ('frontend_child_spawn', struct.pack('<14I', 1, 10, 90, 0, 0, 0, 0, 0, 0, 16398, 16398, 1, 4, 0))]
    lines = [b'unrelated console startup text\r\n']
    for kind, raw in extras:
        lines.extend(chunks(kind, raw))
    for kind in q.RAW_KINDS:
        lines.extend(chunks(kind, records[kind]))
        lines.extend(chunks(kind + '_sha256', hashlib.sha256(records[kind]).digest()))
    lines.append((f'DL30_RESULT_{NONCE} status=FAILED desktop_ok=1 frames_out=20 frames_in=20 packets_out=20 packets_in=20 '
                  'error=fixture_unqualified_wire errno=0 console_bytes_in=0 partial_line=0 partial_bits=0 daemon_cleanup=0 '
                  'frontend_pid=90 mdns_pid=91 app1_pid=101 app2_pid=102 shell1_pid=201 shell2_pid=202 client1_pid=301 client2_pid=302 '
                  'cleanup_complete=0 phases=0 if_error=0 if_stage=0 default_add_attempted=0 route_error=0 route_stage=0 '
                  'close_started_ms=0 close_deadline_ms=0 close_completed_ms=0 finished_ms=100000\n').encode())
    return b''.join(lines)


def altered(records, key, path, value=None, remove=False):
    out = dict(records); doc = json.loads(records[key]); node = doc
    for part in path[:-1]:
        node = node[part]
    if remove:
        del node[path[-1]]
    else:
        node[path[-1]] = value
    out[key] = (json.dumps(doc, separators=(',', ':')) + '\n').encode()
    return out


class EvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.records = fixture_records()
        cls.raw = console(cls.records)
        cls.checks = 0

    def check(self, raw, good=False, **kwargs):
        type(self).checks += 1
        result = q.qualify(raw, NONCE, **kwargs)
        expected = 'VERIFIED_CLOSED_RECORDS' if good else 'FAILED'
        self.assertEqual(result['closed_desktop']['status'], expected, result)
        self.assertFalse(result['overall_runtime_PASS_claimed'])
        return result

    def test_01_complete_source_bound_observations(self):
        result = self.check(self.raw, True, frontend_pid=90)
        self.assertEqual(result['observations']['record_sha256']['app1_pty_tx'], q.digest(b'normal typing'))
        self.assertEqual(result['backend_correspondence']['status'], 'NOT_EVALUATED')
        self.assertEqual(result['closed_desktop']['clients'][0]['client_pid'], 301)
        for cr_count in (1, 2, 3):
            normalized = self.raw.replace(b'\n', b'\r' * cr_count + b'\n')
            observed = self.check(normalized, True)
            self.assertEqual(observed['console_sha256'], q.digest(normalized))
        self.check(self.raw.replace(b' kind=app1_result ', b' kind=app1_\rresult ', 1))
        self.check(self.raw.replace(b'desktop_ok=1', b'desktop_\rok=1', 1))
        self.check(self.raw, frontend_pid=91)
        records = dict(self.records)
        for slot in (1, 2):
            records[f'app{slot}_pty_tx'] = b''
            doc = json.loads(records[f'app{slot}_result'])
            doc['session'].update(pty_tx_bytes=0, input_accepted_bytes=0)
            records[f'app{slot}_result'] = json.dumps(doc).encode()
        self.check(console(records), True)

    def test_02_every_required_record_and_digest_missing(self):
        lines = self.raw.splitlines(keepends=True)
        for kind in q.REQUIRED:
            with self.subTest(kind=kind):
                raw = b''.join(line for line in lines if b' kind=' + kind.encode() + b' ' not in line)
                self.check(raw)

    def test_03_chunk_identity_offset_integrity(self):
        lines = self.raw.splitlines(keepends=True)
        first = next(i for i, line in enumerate(lines) if b' kind=app1_result ' in line)
        changes = [
            ('foreign', lambda x: x.replace(NONCE.encode(), b'f' * 48, 1)),
            ('initial-offset', lambda x: x.replace(b'offset=0 ', b'offset=1 ', 1)),
            ('leading-zero', lambda x: x.replace(b'offset=0 ', b'offset=00 ', 1)),
            ('oversize', lambda x: __import__('re').sub(rb'total=[0-9]+', b'total=65537', x)),
            ('hex-upper', lambda x: x.replace(b'hex=', b'hex=AA', 1)),
            ('trailing', lambda x: x.rstrip(b'\n') + b' extra\n'),
            ('prefix', lambda x: b'noise ' + x),
            ('short', lambda x: x[:-3] + b'\n'),
            ('odd', lambda x: x[:-2] + b'\n'),
        ]
        for name, change in changes:
            mutated = list(lines); mutated[first] = change(mutated[first]); self.check(b''.join(mutated))
        mutated = list(lines); mutated[first], mutated[first + 1] = mutated[first + 1], mutated[first]
        self.check(b''.join(mutated))
        self.check(b''.join(lines[:first + 1] + [lines[first]] + lines[first + 1:]))
        self.check(self.raw + b''.join(chunks('app1_result', self.records['app1_result'])))
        self.check(self.raw[:-1])
        self.check(self.raw + b'DL30_OBSERVE_' + b'e' * 48 + b' kind=anything total=0 offset=0 hex=\n')
        data = dict(self.records); data['app1_pty_tx'] = b'altered bytes'
        altered_console = console(data)
        # Keep wrong old digest: no semantic output can override integrity.
        altered_console = altered_console.replace(hashlib.sha256(data['app1_pty_tx']).hexdigest().encode(), hashlib.sha256(self.records['app1_pty_tx']).hexdigest().encode())
        self.check(altered_console)
        self.check(self.raw + b''.join(chunks('unknown', b'x' * 65536)) * 3)

    def test_04_missing_and_wrong_typed_JSON_fields(self):
        def leaves(value, path=()):
            if type(value) is dict:
                for key, child in value.items():
                    yield from leaves(child, path + (key,))
            else:
                yield path, value
        for key in ('app1_result', 'app1_child_ready', 'app1_child', 'app1_client'):
            for path, value in leaves(json.loads(self.records[key])):
                for remove in (False, True):
                    with self.subTest(key=key, path=path, remove=remove):
                        records = altered(self.records, key, path, {'wrong': 1}, remove=remove)
                        self.check(console(records))

    def test_05_semantic_negative_fields(self):
        for key, path, value in [
            ('app1_result', ('startup', 'client_exit_status'), 1),
            ('app1_result', ('startup', 'client_reaped_by_shell'), False),
            ('app1_result', ('startup', 'foreground_pgid'), 301),
            ('app1_result', ('startup', 'fallback_prompt_observed'), False),
            ('app1_result', ('cleanup', 'status'), 0),
            ('app1_result', ('session', 'term_signal'), 0),
            ('app1_result', ('session', 'input_unsent_bytes'), 1),
            ('app1_client', ('addresses',), ['93.184.216.34', '10.0.2.3']),
            ('app1_client', ('selected',), '93.184.216.35'),
            ('app1_client', ('selected',), '093.184.216.34'),
            ('app1_client', ('ppid',), 101),
            ('app1_client', ('pid',), 101),
            ('app1_client', ('peer_eof',), False),
            ('app1_client', ('request_shutdown',), False),
            ('app1_client', ('socket_closed',), False),
            ('app1_client', ('request_hex',), '00' * 186),
            ('app1_client', ('response_hex',), '00'),
            ('app1_client', ('http', 'body_bytes'), 15),
            ('app1_client', ('started_ms',), (1 << 64) - 1),
            ('app1_client', ('elapsed_ms',), 60000),
            ('app1_client', ('tcp_maxseg_observed',), 257),
            ('app1_child_ready', ('exec_boundary', 'inventory_scope'), 'after_libinfo_threads'),
        ]:
            self.check(console(altered(self.records, key, path, value)))
        records = altered(self.records, 'app2_client', ('local', 'port'), 50001)
        self.check(console(records))
        records = altered(records, 'app2_client', ('addresses',), ['93.184.216.35'])
        records = altered(records, 'app2_client', ('selected',), '93.184.216.35')
        records = altered(records, 'app2_client', ('peer', 'address'), '93.184.216.35')
        self.check(console(records), True)
        records = {k: (v.replace(b'201', b'101') if k.startswith('app1_') else v) for k, v in self.records.items()}
        self.check(console(records))

    def test_06_JSON_admission(self):
        for malformed in (b'{"x":1,"x":2}', b'{"x":1,"\\u0078":2}', b'{"x":{"y":1,"y":2}}',
                          b'{"x":NaN}', b'{"x":1.0}', b'{"x":1e2}', b'{"x":-0}',
                          b'{"x":18446744073709551616}', b'{"x":-9223372036854775809}',
                          b'{"x":"\\u00e9"}', b'{} garbage', b'{"x":' + b'[' * 40 + b'0' + b']' * 40 + b'}'):
            with self.assertRaises(q.Failure):
                q.strict_json(malformed)
            type(self).checks += 1
        q.strict_json(b'{"x":' + b'[' * 30 + b'0' + b']' * 30 + b'}'); type(self).checks += 1
        with self.assertRaises(q.Failure): q.strict_json(b'{"x":' + b'[' * 31 + b'0' + b']' * 31 + b'}')
        type(self).checks += 1
        q.strict_json(b'{"x":[' + b','.join([b'0'] * 4093) + b']}'); type(self).checks += 1
        with self.assertRaises(q.Failure): q.strict_json(b'{"x":[' + b','.join([b'0'] * 4094) + b']}')
        type(self).checks += 1
        records = dict(self.records)
        doc = json.loads(records['app1_client']); doc['unknown'] = {'allowed': [1, False, None, 'a']}
        records['app1_client'] = json.dumps(doc).encode(); self.check(console(records), True)
        for raw in (records['app1_client'].replace(b'{', b'{"u":1,"u":2,', 1),
                    records['app1_client'].replace(b'"none"', b'"none\\u0000"', 1)):
            data = dict(records); data['app1_client'] = raw; self.check(console(data))

    def test_07_raw_marker_prompt_and_grid(self):
        for name, key, value in [
            ('PID-canonical', 'app1_client_pid', b'0301\n'),
            ('shell-return-status', 'app1_shell_return', self.records['app1_shell_return'].replace(b'status=0', b'status=1')),
            ('grid-empty', 'app1_grid', b''), ('grid-ragged', 'app1_grid', b'xxx\n' * 24),
            ('stdout', 'app1_stdout', b'error'), ('stderr', 'app1_stderr', b'error'),
        ]:
            data = dict(self.records); data[key] = value; self.check(console(data))
        raw = self.records['app1_pty_rx']; prompt = b'$ \x1b[?2004h'
        for changed in (raw.replace(prompt, b'$ '), prompt + raw.replace(prompt, b''),
                        raw.replace(b'pid=301 status=0', b'pid=302 status=0'),
                        raw.replace(prompt, self.records['app1_shell_return'] + prompt),
                        raw.replace(b'DL30_CLIENT_BEGIN', b'DL30_CLIENT_IGNORED')):
            data = altered(self.records, 'app1_result', ('session', 'pty_rx_bytes'), len(changed))
            data['app1_pty_rx'] = changed; self.check(console(data))

    def test_08_session_manager_clock_and_order(self):
        data = dict(self.records); data['session_result'] = data['session_result'].replace(b'elapsed_ms=200', b'elapsed_ms=50')
        self.check(console(data), True)
        for key, before, after in [
            ('session_result', b'cleanup=Complete', b'cleanup=Incomplete'),
            ('session_result', b'wait=Some(0)', b'wait=Some(9)'),
            ('session_result', b'observed_flags: 16398', b'observed_flags: 0'),
            ('session_result', b'gate=AppReady observed_ms=Some(20)', b'gate=AppReady observed_ms=None'),
            ('session_result', b'gate=KillAttempt observed_ms=None', b'gate=KillAttempt observed_ms=Some(101)'),
            ('manager_status', b'observed_flags: 16398', b'observed_flags: 0'),
            ('manager_status', b'raw_app_wait=Some(0)', b'raw_app_wait=Some(9)'),
            ('manager_status', b'ms=20 event=AppReady', b'ms=21 event=AppReady'),
            ('manager_status', b'event=AppReady', b'event=Failure'),
            ('manager_status', b'attempt=0', b'attempt=00'),
        ]:
            data = dict(self.records); data[key] = data[key].replace(before, after, 1); self.check(console(data))
        for key in ('session_result', 'manager_status'):
            for size in range(0, len(self.records[key]), 101):
                data = dict(self.records); data[key] = data[key][:size]; self.check(console(data))

    def test_09_actual_frontend_closure_required(self):
        self.check(console(self.records, extra=[]))
        for before, after in ((b'reaped=1', b'reaped=0'), (b'wait_status=0', b'wait_status=9'),
                              (b'output_eof=1', b'output_eof=0'), (b'stderr_eof=1', b'stderr_eof=0'),
                              (b'kill_sent=0', b'kill_sent=1'), (b'lost=0', b'lost=1')):
            # Front metadata spans multiplechunks, modify decoded stream and re-encode.
            _, extras, _ = q.observe_records(self.raw, NONCE)
            rows = [('frontend_child', extras['frontend_child'][0].replace(before, after)),
                    ('frontend_child_spawn', extras['frontend_child_spawn'][0])]
            self.check(console(self.records, extra=rows))
        _, extras, _ = q.observe_records(self.raw, NONCE)
        rows = [('frontend_child', extras['frontend_child'][0]), ('frontend_child_spawn', b'\0' * 56)]
        self.check(console(self.records, extra=rows))

    def test_10_producer_final_stability_seal(self):
        self.check(self.raw.replace(b'desktop_ok=1', b'desktop_ok=0'))
        self.check(self.raw.replace(b'client1_pid=301', b'client1_pid=0'))
        line = self.raw.splitlines(keepends=True)[-1]
        self.check(self.raw[:-len(line)])
        self.check(self.raw + line)
        self.check(line + self.raw[:-len(line)])
        result = self.check(self.raw, True)
        self.assertEqual(result['closed_desktop']['producer_closed_record_seal']['producer_status_preserved'], 'FAILED')

    def test_11_independent_HTTP_cases(self):
        h = b'HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n'
        for wire, framing in ((h + b'Content-Length: 14\r\n\r\nExample Domain', 1),
                              (h + b'Transfer-Encoding: chunked\r\n\r\n7\r\nExample\r\n7\r\n Domain\r\n0\r\n\r\n', 2),
                              (h + b'\r\nExample Domain', 3)):
            facts = q.http_facts(wire); self.assertEqual((facts['body_bytes'], facts['framing']), (14, framing)); type(self).checks += 1
        for wire in (h + b'Content-Length: 15\r\n\r\nExample Domain', h + b'Content-Length: 13\r\n\r\nExample Domain',
                     h + b'Content-Type: text/html\r\n\r\nExample Domain', h + b'Content-Encoding: gzip\r\n\r\nExample Domain',
                     h + b'Transfer-Encoding: chunked\r\nContent-Length: 14\r\n\r\nExample Domain',
                     h + b'Transfer-Encoding: chunked\r\n\r\ne\r\nExample Domain\r\n0\r\nX: y\r\n\r\n',
                     h + b'\r\nnot example', h.replace(b'200', b'201') + b'\r\nExample Domain'):
            with self.assertRaises(q.Failure): q.http_facts(wire)
            type(self).checks += 1

    def test_12_backend_schema_unbound_never_promoted(self):
        with tempfile.TemporaryDirectory(prefix='desktop-evidence-backend-') as temp:
            path = Path(temp) / 'result.json'
            path.write_text(json.dumps({'schema': 1, 'profile': q.PROFILE, 'nonce': NONCE, 'complete': True,
                                        'egress': {'complete': True, 'terminal_reset_events': [{'forwarded': True}]}}))
            result = self.check(self.raw, True, backend_result=path)
            self.assertEqual(result['backend_correspondence']['status'], 'NOT_EVALUATED')
            path.write_text('{}')
            result = self.check(self.raw, True, backend_result=path)
            self.assertEqual(result['backend_correspondence']['status'], 'FAILED')
            path.unlink()
            result = self.check(self.raw, True, backend_result=path)
            self.assertEqual(result['backend_correspondence']['status'], 'FAILED')

    def test_13_CLI_real_files_and_errors(self):
        with tempfile.TemporaryDirectory(prefix='desktop-evidence-CLI-') as temp:
            path = Path(temp) / 'console'; path.write_bytes(self.raw)
            command = [sys.executable, '-B', str(Path(q.__file__)), '--console', str(path), '--nonce', NONCE]
            result = subprocess.run(command, capture_output=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout)['closed_desktop']['status'], 'VERIFIED_CLOSED_RECORDS')
            path.write_bytes(b'no records')
            result = subprocess.run(command, capture_output=True, timeout=20)
            self.assertEqual(result.returncode, 1); self.assertEqual(json.loads(result.stdout)['closed_desktop']['status'], 'FAILED')
            path.unlink(); path.symlink_to('/dev/null')
            result = subprocess.run(command, capture_output=True, timeout=20)
            self.assertEqual(result.returncode, 1); self.assertNotIn(b'Traceback', result.stderr)


if __name__ == '__main__':
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(EvidenceTests))
    print(json.dumps({'groups': result.testsRun, 'checks': EvidenceTests.checks, 'failures': len(result.failures), 'errors': len(result.errors)}))
    raise SystemExit(0 if result.wasSuccessful() else 1)
