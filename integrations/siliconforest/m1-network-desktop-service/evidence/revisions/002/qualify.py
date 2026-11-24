#!/usr/bin/env python3
"""Original offline closed-desktop qualifier. No C proof, network or target calls."""
import argparse
import hashlib
import ipaddress
import json
import math
import os
from pathlib import Path
import re
import stat
import struct
import sys
sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parents[2]  # Stable evidence root; this revision is nested.
BASE = HERE.parent
ROOT = HERE.parents[3]
PROFILE = 'desktop-native-resolver-two-http-v1'
MAX_CONSOLE = 32 * 1024 * 1024
APP_KEYS = ('result', 'child_ready', 'child', 'client', 'client_pid',
            'shell_return', 'pty_rx', 'pty_tx', 'grid', 'stdout', 'stderr')
RAW_KINDS = ('session_result', 'manager_status') + tuple(
    f'app{slot}_{key}' for slot in (1, 2) for key in APP_KEYS)
REQUIRED = set(RAW_KINDS) | {kind + '_sha256' for kind in RAW_KINDS}
GATES = ('Admission', 'StatusNonblocking', 'SpawnReturned', 'PidOwned',
         'AppReady', 'AppCleanup', 'StatusEof', 'AppReaped', 'TermAttempt', 'KillAttempt')
PINS = {
    'desktop_proof/source-validation-001.json': '888ffb9124127f3c2f7edc705908a428f9c85c19dadeab99e9b5b3d394821f47',
    'record_guard/source-validation-001.json': '2a8976145591d2b5bbbba0390a75e2dd275287cc6a01915eaea1b77175dd186b',
    'session/frontend-binding-002.json': '965aba83b00122b382065306ac8e3b6e98646988ae7bb7e96b77a0477959990f',
    'session/revisions/002/source-validation-clock-002.json': '4f27de0e2b2227d1f88a039d3ef1fe481438205fe79e9b4d0570a633f17763e7',
    'revisions/002/client-source-validation-001.json': '746c298a0213d49398dc4588af6c52656df51c7710245fffb168616c2d83a09f',
    'revisions/002/terminal-source-validation-002.json': 'fa6f13a447c07846ee466f613eaa4f7b0ff2a751106286da7f989668b691a1a5',
}

EXTRA_SOURCES = {
 'integrations/puredarwin/m1-network-desktop-service/session/desktop_network.c': '716def952a89a2fdbab3bc3843986324b855095a7aed3f5c70cfd7bd742473aa',
 'integrations/puredarwin/m1-network-desktop-service/session/revisions/003/desktop_network.c': 'daccfbd607cb7c3b03011ed6ea23b7969dee562c436b0ac7a9abd1dd7c51d519',
 'integrations/puredarwin/m1-network-desktop-service/session/owned_child.c': '62bd843d88777893916da380cf71156335ec0816b9b064916586dc5d60cf45cb',
 'integrations/puredarwin/m1-network-desktop-service/session/owned_child.h': '29a4970f9e7c897b0f4c86a6bdb7cac190fc2e8bc16b2f7d513c78afb5e9b9b8',
 'integrations/puredarwin/chonkstep-darwin/darwin-spawn/darwin_spawn.h': '080c2e0c9694951bd8d8a689811b8c6bc525d76392aff2f8e2af792ebf947d4c',
}


class Failure(ValueError):
    def __init__(self, code, detail=''):
        self.code, self.detail = code, str(detail)[:240]
        super().__init__(code + (': ' + self.detail if detail else ''))


def need(condition, code, detail=''):
    if not condition:
        raise Failure(code, detail)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def load_file(path, cap):
    """Bounded regular-file snapshot; no shell, writes, retries or device files."""
    flags = os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW | os.O_CLOEXEC
    fd = os.open(path, flags)
    try:
        before = os.fstat(fd)
        need(stat.S_ISREG(before.st_mode) and 0 <= before.st_size <= cap, 'file_bound', Path(path).name)
        data = bytearray()
        while len(data) <= cap:
            chunk = os.read(fd, min(65536, cap + 1 - len(data)))
            if not chunk:
                break
            data.extend(chunk)
        after = os.fstat(fd)
        attrs = ('st_dev', 'st_ino', 'st_size', 'st_mtime_ns', 'st_ctime_ns')
        need(all(getattr(before, k) == getattr(after, k) for k in attrs), 'file_changed', Path(path).name)
        need(len(data) == before.st_size and len(data) <= cap, 'file_length', Path(path).name)
        return bytes(data)
    finally:
        os.close(fd)


def strict_json(raw, cap=16384, integers_only=True):
    need(type(raw) is bytes and 0 < len(raw) <= cap, 'json_size')
    try:
        text = raw.decode('ascii')
    except UnicodeError as error:
        raise Failure('json_ascii') from error

    def integer(token):
        value = int(token)
        need(-(1 << 63) <= value < (1 << 64), 'json_integer_range')
        need(token != '-0', 'json_negative_zero')
        return value

    def floating(token):
        need(not integers_only, 'json_float')
        value = float(token)
        need(math.isfinite(value), 'json_nonfinite')
        return value

    def constant(_):
        raise Failure('json_nonfinite')

    def pairs(items):
        obj = {}
        for key, value in items:
            need(key not in obj, 'json_duplicate', key)
            obj[key] = value
        return obj

    try:
        value = json.loads(text, parse_int=integer, parse_float=floating,
                           parse_constant=constant, object_pairs_hook=pairs)
    except (json.JSONDecodeError, RecursionError) as error:
        raise Failure('json_syntax', type(error).__name__) from error
    tokens = 0

    def visit(item, depth):
        nonlocal tokens
        tokens += 1
        need(tokens <= (4096 if integers_only else 65536) and depth <= 32, 'json_complexity')
        if type(item) is str:
            need(all(ord(c) <= 127 for c in item), 'json_ascii')
        elif type(item) is dict:
            for key, child in item.items():
                visit(key, depth)
                visit(child, depth + 1)
        elif type(item) is list:
            for child in item:
                visit(child, depth + 1)
    visit(value, 1)
    need(type(value) is dict, 'json_root_object')
    return value


def source_bindings():
    for rel, expected in PINS.items():
        path = BASE / rel
        raw = load_file(path, 1024 * 1024)
        need(digest(raw) == expected, 'source_receipt', rel)
        receipt = json.loads(raw)
        for name, expected_file in receipt.get('files_sha256', {}).items():
            need(digest(load_file(path.parent / name, 4 * 1024 * 1024)) == expected_file,
                 'source_file', name)
        if rel.startswith('desktop_proof/'):
            for name, expected_file in receipt['inputs_sha256'].items():
                need(digest(load_file(ROOT / name, 4 * 1024 * 1024)) == expected_file,
                     'source_input', name)
    for rel, expected in EXTRA_SOURCES.items():
        need(digest(load_file(ROOT / rel, 1048576)) == expected, 'source_child_protocol', rel)
    return {'receipts': dict(PINS), 'child_protocol_sources': dict(EXTRA_SOURCES)}


def field(obj, path, kind):
    current = obj
    for key in path.split('.'):
        need(type(current) is dict and key in current, 'missing_field', path)
        current = current[key]
    need(type(current) is kind, 'field_type', path)
    return current


def equal(obj, path, expected):
    need(field(obj, path, type(expected)) == expected, 'field_value', path)


def integer_field(obj, path, low=0, high=(1 << 64) - 1):
    value = field(obj, path, int)
    need(low <= value <= high, 'field_range', path)
    return value


def null_field(obj, path):
    equal(obj, path, None)


def nonce_valid(nonce):
    need(type(nonce) is str and re.fullmatch('[0-9a-f]{48}', nonce) is not None, 'nonce')


def observe_records(console, nonce):
    nonce_valid(nonce)
    need(type(console) is bytes and len(console) <= MAX_CONSOLE, 'console_bound')
    expression = re.compile(rb'DL30_OBSERVE_([0-9a-f]{48}) kind=([a-z0-9_]{1,63}) total=(0|[1-9][0-9]{0,5}) offset=(0|[1-9][0-9]{0,5}) hex=([0-9a-f]*)')
    records = {}; extras = {}; order = []; pending = None; line_count = 0; encoded = 0
    # split LF only: control bytes cannot masquerade as observation delimiters.
    console_lines = console.split(b'\n')
    for number, line in enumerate(console_lines, 1):
        if b'DL30_OBSERVE_' not in line:
            continue
        need(number < len(console_lines), 'observation_missing_LF')
        line = line.rstrip(b'\r')  # TTY layers may append multiple CRs before LF.
        m = expression.fullmatch(line)
        need(m is not None, 'observation_syntax', number)
        got_nonce, raw_kind, raw_total, raw_offset, raw_hex = m.groups()
        need(got_nonce.decode() == nonce, 'foreign_observation_nonce', number)
        kind = raw_kind.decode(); total = int(raw_total); offset = int(raw_offset)
        need(total <= 65536 and offset <= total, 'observation_bound', kind)
        need(len(raw_hex) <= 128 and len(raw_hex) % 2 == 0, 'observation_hex', kind)
        chunk = bytes.fromhex(raw_hex.decode())
        need(len(chunk) == min(64, total - offset), 'observation_chunk_length', kind)
        need(total != 0 or (offset == 0 and not chunk), 'observation_empty', kind)
        line_count += 1; encoded += len(line) + 1
        need(line_count <= 8192 and encoded <= 524288, 'observation_budget')
        if pending is None:
            need(offset == 0, 'observation_initial_offset', kind)
            need(kind not in records, 'observation_duplicate', kind)
            pending = (kind, total, bytearray())
        pkind, ptotal, data = pending
        need(kind == pkind and total == ptotal, 'observation_interleave_or_total', kind)
        need(offset == len(data), 'observation_offset', kind)
        data.extend(chunk)
        if len(data) == total:
            if kind in REQUIRED:
                records[kind] = bytes(data)
            else:
                extras.setdefault(kind, []).append(bytes(data))
            order.append(kind); pending = None
    need(pending is None, 'observation_incomplete')
    need(REQUIRED <= records.keys(), 'observation_missing', ','.join(sorted(REQUIRED - records.keys())))
    for kind in RAW_KINDS:
        proof = records[kind + '_sha256']
        need(len(proof) == 32 and proof.hex() == digest(records[kind]), 'record_digest', kind)
        pos = order.index(kind)
        need(pos + 1 < len(order) and order[pos + 1] == kind + '_sha256', 'digest_order', kind)
    wanted = [part for kind in RAW_KINDS for part in (kind, kind + '_sha256')]
    need([kind for kind in order if kind in REQUIRED] == wanted, 'producer_record_order')
    return {kind: records[kind] for kind in RAW_KINDS}, extras, {
        'observation_lines': line_count, 'encoded_observation_bytes': encoded,
        'record_sha256': {kind: digest(records[kind]) for kind in RAW_KINDS},
        'raw_record_bytes': sum(len(records[kind]) for kind in RAW_KINDS)}


def ascii_lines(raw, cap, maxline):
    need(0 < len(raw) <= cap and raw.endswith(b'\n'), 'text_bound')
    need(all(b == 10 or 32 <= b <= 126 for b in raw), 'text_ascii')
    lines = raw.decode('ascii').splitlines()
    need(all(0 < len(line) <= maxline for line in lines), 'text_line_bound')
    return lines


def decimal(text, maximum=(1 << 64) - 1):
    need(re.fullmatch('0|[1-9][0-9]*', text) is not None and len(text) <= 20, 'decimal')
    value = int(text)
    need(value <= maximum, 'decimal_range')
    return value


def native_text(pid):
    return ('SpawnResultV1 { version: 1, stage: 10, pid: ' + str(pid) +
            ', primary_error: 0, spawn_error: 0, attr_destroy_error: 0, actions_destroy_error: 0, '
            'fd_close_error: 0, fd_close_failures: 0, requested_flags: 16398, observed_flags: 16398, '
            'child_created: 1, duplicate_count: 4, reserved: 0 }')


def cleanup_text(app):
    return ('Some(Cleanup { complete: true, raw_wait_status: ' + str(app['shell_wait']) +
            ', term_sent: ' + str(app['shell_term']).lower() + ', kill_sent: ' +
            str(app['shell_kill']).lower() + ' })')


def session_text(raw, nonce):
    lines = ascii_lines(raw, 16384, 4096)
    need(len(lines) == 31, 'session_line_count')
    equal({'line': lines[0]}, 'line', 'CHONKDESKTOP/1 initialized=1 input=both shell=shared native_runtime_qualification=not_inferred')
    header = re.fullmatch(r'CHONKDESKTOP/1 elapsed_ms=([0-9]+) cleanup=Complete deadline=(true|false) frame_error=None manager_failed=false', lines[1])
    need(header is not None, 'session_header')
    elapsed = decimal(header[1], 180000)
    need(lines[2] == 'CHONKLAUNCH/1 snapshot attempts=2 journal_error=None app_internal_gates=unknown_until_final_app_record', 'session_manager_header')
    apps = []
    for slot in (1, 2):
        at = 3 + (slot - 1) * 14; instance = f'{slot:04d}' + nonce[:44]; a = slot - 1
        prefix = f'attempt={a} source=explicit_startup request=Some({slot}) instance=Some("{instance}") pid=Some('
        need(lines[at].startswith(prefix), 'session_instance')
        m = re.fullmatch(r'([0-9]+)\) created_ms=([0-9]+) failure=None failure_ms=None status_bytes=([0-9]+) wait=Some\(0\) wait_error=None cleanup=Some\(Cleanup \{ complete: true, raw_wait_status: ([0-9]+), term_sent: (true|false), kill_sent: (true|false) \}\) native_return=Some\(0\) native=Some\((.*)\)', lines[at][len(prefix):])
        need(m is not None, 'session_attempt')
        pid = decimal(m[1], 2147483647); created = decimal(m[2], 180000)
        size = decimal(m[3], 4096); wait = decimal(m[4], 65535)
        need(pid > 1 and size > 0 and m[7] == native_text(pid), 'session_spawn')
        term, kill = m[5] == 'true', m[6] == 'true'
        need(not kill or term, 'session_shell_signal_order')
        for index, label in enumerate(('exit_reason=', 'cs_at_spawn=', 'kernel_messages='), 1):
            need(lines[at + index].startswith(f'attempt={a} {label}') and len(lines[at + index]) > len(f'attempt={a} {label}'), 'session_diagnostic')
        times = {}
        for i, gate in enumerate(GATES):
            m = re.fullmatch(re.escape(f'attempt={a} gate={gate} observed_ms=') + r'(None|Some\(([0-9]+)\))', lines[at + 4 + i])
            need(m is not None, 'session_gate', gate)
            value = None if m[1] == 'None' else decimal(m[2], 180000)
            need(i >= 8 or value is not None, 'session_missing_gate', gate)
            need(value is None or value >= created, 'session_created_order')
            times[gate] = value
        first = [times[k] for k in GATES[:5]]
        need(first == sorted(first), 'session_start_order')
        need(all(times[k] >= times['AppReady'] for k in GATES[5:8]), 'session_finish_order')
        need(times['KillAttempt'] is None, 'manager_killed_app')
        need(times['TermAttempt'] is None or times['AppReady'] <= times['TermAttempt'] <= times['AppReaped'], 'manager_term_order')
        apps.append(dict(slot=slot, instance=instance, pid=pid, created_ms=created,
                         status_bytes=size, shell_wait=wait, shell_term=term,
                         shell_kill=kill, times=times))
    need(apps[0]['pid'] != apps[1]['pid'], 'app_pid_duplicate')
    # Deliberately no comparison between manager timestamps and NativeIo elapsed.
    return {'elapsed_ms': elapsed, 'deadline': header[2] == 'true', 'apps': apps}


def manager_text(raw, session, nonce):
    lines = ascii_lines(raw, 65536, 1024)
    need(len(lines) <= 513 and lines[0] == 'CHONKLAUNCH/1 scope=manager app_internal_gates=unknown_until_final_app_record', 'manager_header')
    seen = [set(), set()]; requested = [False, False]; native = [False, False]
    final = [False, False]; status_bytes = [0, 0]; diagnostics = [set(), set()]
    last_ms = 0; manager_cs = False
    for line in lines[1:]:
        if line.startswith('CHONKLAUNCH/1 scope=manager self_cs_status='):
            need(not manager_cs and line.split('=', 2)[-1], 'manager_cs_duplicate')
            manager_cs = True; continue
        m = re.fullmatch(r'CHONKLAUNCH/1 attempt=([01]) (.*)', line)
        need(m is not None, 'manager_line')
        a = int(m[1]); tail = m[2]; app = session['apps'][a]
        if tail.startswith('ms='):
            tm = re.fullmatch(r'ms=([0-9]+) (.*)', tail)
            need(tm is not None, 'manager_time')
            ms = decimal(tm[1], 180000); need(ms >= last_ms, 'manager_clock_regressed'); last_ms = ms
            tail = tm[2]
            request = f'request=Some({a + 1}) instance=Some("{app["instance"]}") source=explicit_startup event=requested'
            if tail == request:
                need(not requested[a] and ms == app['created_ms'], 'manager_request')
                need(a == 0 or 'AppReady' in seen[0], 'second_start_before_first_ready')
                requested[a] = True; continue
            need(requested[a] and tail.startswith('event='), 'manager_event')
            event = tail[6:]
            if event in GATES:
                index = GATES.index(event)
                need(event not in seen[a] and app['times'][event] == ms, 'manager_gate', event)
                need(index == 0 or (GATES[index - 1] in seen[a] if index < 4 else 'PidOwned' in seen[a]), 'manager_start_order')
                need(index < 5 or 'AppReady' in seen[a], 'manager_completion_order')
                need(event != 'PidOwned' or native[a], 'manager_missing_spawn')
                seen[a].add(event); continue
            labels = ('CsStatus phase=Spawn cs_status=', 'CsStatus phase=Exit cs_status=', 'ExitReason exit_reason=', 'KernelMessages kernel_messages=')
            matches = [label for label in labels if event.startswith(label) and len(event) > len(label)]
            need(len(matches) == 1 and 'PidOwned' in seen[a] and matches[0] not in diagnostics[a], 'manager_diagnostic')
            diagnostics[a].add(matches[0]); continue
        need(requested[a], 'manager_unrequested')
        if tail == 'native_return=0 native=' + native_text(app['pid']):
            need(not native[a] and 'SpawnReturned' in seen[a] and 'PidOwned' not in seen[a], 'manager_native_order')
            native[a] = True; continue
        m = re.fullmatch(r'status_bytes=([0-9]+) raw_app_wait=(None|Some\(0\)) wait_error=None cleanup=(.*)', tail)
        need(m is not None and 'AppReady' in seen[a] and not final[a], 'manager_status')
        count = decimal(m[1], 4096)
        need(status_bytes[a] <= count <= app['status_bytes'], 'manager_status_count'); status_bytes[a] = count
        waited = m[2] == 'Some(0)'; cleaned = m[3] == cleanup_text(app)
        need(cleaned or m[3] == 'None', 'manager_cleanup')
        need(not waited or 'AppReaped' in seen[a], 'manager_wait_order')
        need(not cleaned or 'AppCleanup' in seen[a], 'manager_cleanup_order')
        if waited and cleaned and count == app['status_bytes'] and set(GATES[:8]) <= seen[a]:
            final[a] = True
    for a in (0, 1):
        expected = {gate for gate, ms in session['apps'][a]['times'].items() if ms is not None}
        need(requested[a] and native[a] and final[a] and seen[a] == expected, 'manager_incomplete', a)


def frontend_closure(extras, frontend_pid):
    rows = extras.get('frontend_child', [])
    need(len(rows) == 1, 'frontend_child_missing_or_duplicate')
    raw = rows[0]
    expression = (rb'kind=([0-9]+) pid=([0-9]+) created=([01]) reaped=([01]) lost=([01]) error=([0-9]+) wait_status=([0-9]+) output_bytes=([0-9]+) stderr_bytes=([0-9]+) output_eof=([01]) stderr_eof=([01]) term_sent=([01]) kill_sent=([01]) stop_requested=([01]) started_ms=([0-9]+) last_ms=([0-9]+)')
    m = re.fullmatch(expression, raw)
    need(m is not None, 'frontend_child_shape')
    keys = ('kind', 'pid', 'created', 'reaped', 'lost', 'error', 'wait_status', 'output_bytes', 'stderr_bytes', 'output_eof', 'stderr_eof', 'term_sent', 'kill_sent', 'stop_requested', 'started_ms', 'last_ms')
    row = {key: decimal(value.decode()) for key, value in zip(keys, m.groups())}
    for key, value in dict(kind=2, created=1, reaped=1, lost=0, error=0, wait_status=0, output_eof=1, stderr_eof=1, kill_sent=0).items():
        need(row[key] == value, 'frontend_not_closed', key)
    need(2 <= row['pid'] <= 2147483647 and 0 < row['started_ms'] <= row['last_ms'], 'frontend_identity_clock')
    need(not row['term_sent'] or row['stop_requested'], 'frontend_signal_order')
    if frontend_pid is not None:
        need(type(frontend_pid) is int and frontend_pid == row['pid'], 'frontend_pid_argument')
    for key, record in (('output_bytes', 'frontend_child_stdout'), ('stderr_bytes', 'frontend_child_stderr')):
        need(row[key] <= 65536, 'frontend_output_cap')
        values = extras.get(record, [])
        need((len(values) == 1 and len(values[0]) == row[key]) if row[key] else not values, 'frontend_pipe_capture', key)
    spawn = extras.get('frontend_child_spawn', [])
    need(len(spawn) == 1 and len(spawn[0]) == 56, 'frontend_spawn_missing')
    fields = struct.unpack('<14I', spawn[0])
    need(fields == (1, 10, row['pid'], 0, 0, 0, 0, 0, 0, 16398, 16398, 1, 4, 0), 'frontend_spawn_ABI')
    return row


def exec_boundary(obj, key):
    for name in ('pre_exec_fd_check_observed', 'exec_status_pipe_eof_observed', 'shell_exec_checked'):
        equal(obj, key + '.' + name, True)
    equal(obj, key + '.inventory_scope', 'pre_exec_only_not_after_exec_inventory')
    return integer_field(obj, key + '.pre_exec_fd_scan_bound_exclusive', 3, 1048576)


def ipv4(text, require_global=False):
    need(type(text) is str and re.fullmatch(r'(?:0|[1-9][0-9]{0,2})(?:\.(?:0|[1-9][0-9]{0,2})){3}', text) is not None, 'IPv4_text')
    try:
        ip = ipaddress.IPv4Address(text)
    except ipaddress.AddressValueError as error:
        raise Failure('IPv4_value') from error
    if require_global and text not in ('192.0.0.9', '192.0.0.10'):
        # Fixed original eg policy, not Python's evolving is_global database.
        denied = ('0.0.0.0/8', '10.0.0.0/8', '100.64.0.0/10', '127.0.0.0/8',
                  '169.254.0.0/16', '172.16.0.0/12', '192.0.0.0/24', '192.0.2.0/24',
                  '192.168.0.0/16', '198.18.0.0/15', '198.51.100.0/24', '203.0.113.0/24', '224.0.0.0/3')
        need(not any(ip in ipaddress.IPv4Network(net) for net in denied), 'IPv4_not_global')
    return str(ip)


def from_hex(obj, key, cap):
    value = field(obj, key, str)
    need(len(value) % 2 == 0 and len(value) <= 2 * cap and re.fullmatch('[0-9a-f]*', value) is not None, 'lower_hex', key)
    return bytes.fromhex(value)


def request_bytes(nonce):
    return (f'GET / HTTP/1.1\r\nHost: example.com\r\nUser-Agent: darwin-lab-egress/1\r\n'
            f'Accept-Encoding: identity\r\nX-Darwin-Lab-Nonce: {nonce}\r\nConnection: close\r\n\r\n').encode('ascii')


def http_facts(data):
    """Independent complete-message parser: no frozen Python/C parser call."""
    need(type(data) is bytes and 0 < len(data) <= 2048, 'http_size')
    head, separator, body = data.partition(b'\r\n\r\n')
    need(separator == b'\r\n\r\n', 'http_headers_incomplete')
    lines = head.split(b'\r\n')
    need(re.fullmatch(rb'HTTP/1\.[01] 200(?: [ -~]*)?', lines[0]) is not None, 'http_status')
    headers = {}
    for line in lines[1:]:
        key, colon, value = line.partition(b':')
        need(colon and re.fullmatch(rb'[A-Za-z0-9-]+', key) is not None, 'http_header_name')
        key = key.lower(); value = value.strip(b' \t')
        need(key not in headers, 'http_header_duplicate')
        need(all(byte == 9 or 32 <= byte <= 126 for byte in value), 'http_header_value')
        headers[key] = value
    need(headers.get(b'content-encoding', b'identity').lower() == b'identity', 'http_encoding')
    need(headers.get(b'content-type', b'').split(b';', 1)[0].strip(b' \t').lower() == b'text/html', 'http_type')
    if b'transfer-encoding' in headers:
        need(b'content-length' not in headers and headers[b'transfer-encoding'].lower() == b'chunked', 'http_ambiguous_framing')
        remaining = body; pieces = []; ended = False
        for _ in range(1024):
            count, crlf, rest = remaining.partition(b'\r\n')
            need(crlf and re.fullmatch(rb'[0-9a-fA-F]{1,8}', count) is not None, 'http_chunk_header')
            count = int(count, 16)
            if count == 0:
                need(rest == b'\r\n', 'http_chunk_trailer_or_extra')
                ended = True; break
            need(count <= 2048 and len(rest) >= count + 2 and rest[count:count + 2] == b'\r\n', 'http_chunk_data')
            pieces.append(rest[:count]); remaining = rest[count + 2:]
        need(ended, 'http_chunk_count')
        body = b''.join(pieces); framing = 2
    elif b'content-length' in headers:
        count = headers[b'content-length']
        need(re.fullmatch(rb'[0-9]{1,6}', count) is not None, 'http_content_length')
        need(len(body) == int(count), 'http_body_length'); framing = 1
    else:
        framing = 3
    need(b'Example Domain' in body, 'http_content')
    return dict(status=200, header_bytes=len(head) + 4, body_bytes=len(body),
                framing=framing, framing_complete=True, eof_observed=True, example_content=True)


def client_record(raw, nonce, app, shell, client):
    doc = strict_json(raw)
    expected = dict(version=1, profile=PROFILE, nonce=nonce, instance=app['instance'],
                    slot=app['slot'], pid=client, ppid=shell, sid=shell, pgid=shell,
                    foreground_pgid=shell, status='PASS', error='none', error_errno=0,
                    gai_rc=0, system_errno=0, tcp_maxseg_request=256, tcp_maxseg_observed=256)
    for name, value in expected.items():
        equal(doc, name, value)
    for name in ('address_list_complete', 'entry_fd_check_observed', 'request_shutdown',
                 'peer_eof', 'socket_created', 'socket_closed'):
        equal(doc, name, True)
    integer_field(doc, 'entry_fd_scan_bound_exclusive', 3, 65536)
    addresses = field(doc, 'addresses', list)
    need(1 <= len(addresses) <= 32, 'client_address_count')
    for address in addresses:
        ipv4(address, require_global=True)
    equal(doc, 'selected', addresses[0]); equal(doc, 'peer.address', addresses[0])
    equal(doc, 'peer.port', 80); equal(doc, 'local.address', '10.0.2.15')
    port = integer_field(doc, 'local.port', 1024, 65535)
    request = from_hex(doc, 'request_hex', 186)
    response = from_hex(doc, 'response_hex', 2048)
    need(request == request_bytes(nonce) and len(request) == 186, 'client_request')
    equal(doc, 'request_bytes', len(request)); equal(doc, 'response_bytes', len(response))
    actual_http = http_facts(response)
    for name, value in actual_http.items():
        equal(doc, 'http.' + name, value)
    started = integer_field(doc, 'started_ms', 1)
    elapsed = integer_field(doc, 'elapsed_ms', 0, 59999)
    need(started + elapsed < (1 << 64), 'client_clock_overflow')
    return {'slot': app['slot'], 'app_pid': app['pid'], 'shell_pid': shell, 'client_pid': client,
            'instance': app['instance'], 'API_addresses_ordered': addresses,
            'tuple': ['10.0.2.15', port, addresses[0], 80],
            'request': request, 'response': response, 'HTTP': actual_http,
            'started_ms': started, 'elapsed_ms': elapsed}


def pty_markers(raw, nonce, instance, shell, client):
    expected = (
        f'DL30_CLIENT_BEGIN session={nonce} instance={instance} pid={client} shell_pid={shell}',
        f'DL30_CLIENT_DONE session={nonce} instance={instance} pid={client} status=0',
        f'DL30_SHELL_RETURN session={nonce} instance={instance} shell_pid={shell} client_pid={client} status=0')
    prefixes = (b'DL30_CLIENT_BEGIN', b'DL30_CLIENT_DONE', b'DL30_SHELL_RETURN')
    stage = 0; offset = 0; end_marker = None
    lines = raw.split(b'\n')
    for line in lines[:-1]:
        clean = line.rstrip(b'\r')
        selected = [i for i, prefix in enumerate(prefixes) if clean.startswith(prefix)]
        if selected:
            i = selected[0]
            need(i == stage and clean == expected[i].encode(), 'pty_marker_order_or_identity')
            stage += 1
            if stage == 3:
                end_marker = offset + len(line) + 1
        offset += len(line) + 1
    need(stage == 3 and end_marker is not None, 'pty_missing_marker')
    need(b'$ \x1b[?2004h' in raw[end_marker:], 'pty_postmarker_prompt')
    return (expected[2] + '\n').encode()


def grid_shape(raw):
    try:
        text = raw.decode('utf-8')
    except UnicodeError as error:
        raise Failure('grid_utf8') from error
    lines = text.split('\n')
    need(6 <= len(lines) <= 60 and 20 <= len(lines[0]) <= 160, 'grid_dimensions')
    need(all(len(line) == len(lines[0]) for line in lines), 'grid_rectangle')
    need(all(char == '\ufffd' or 32 <= ord(char) <= 126 for line in lines for char in line), 'grid_character')


def app_records(records, nonce, app):
    prefix = f'app{app["slot"]}_'
    raw = lambda name: records[prefix + name]
    result = strict_json(raw('result'))
    for key, value in dict(schema=1, instance=app['instance'], app_pid=app['pid'], shell='/bin/sh', term='dumb').items():
        equal(result, key, value)
    shell = integer_field(result, 'child_pid', 2, 2147483647)
    client = integer_field(result, 'startup.client_pid', 2, 2147483647)
    bound = exec_boundary(result, 'child_exec_boundary')
    need(exec_boundary(result, 'session.exec_boundary') == bound, 'exec_bound_mismatch')
    for key, value in {'cleanup.complete': True, 'cleanup.status': app['shell_wait'],
                       'cleanup.term_sent': app['shell_term'], 'cleanup.kill_sent': app['shell_kill'],
                       'cleanup.error': None, 'errors': []}.items():
        equal(result, key, value)
    for name in ('ready', 'keymap', 'pty_exec_checked', 'ssd'):
        equal(result, 'session.' + name, True)
    integer_field(result, 'session.configure_serial', 1, (1 << 32) - 1)
    integer_field(result, 'session.frames', 1); integer_field(result, 'session.commits', 1)
    integer_field(result, 'session.unsupported_terminal_sequences')
    equal(result, 'session.pty_rx_bytes', len(raw('pty_rx')))
    need(len(raw('pty_rx')) > 0, 'empty_pty_rx')
    equal(result, 'session.pty_tx_bytes', len(raw('pty_tx')))
    equal(result, 'session.input_accepted_bytes', len(raw('pty_tx')))
    equal(result, 'session.input_unsent_bytes', 0)
    closed = field(result, 'session.window_closed', bool)
    eof = field(result, 'session.pty_eof', bool)
    signaled = field(result, 'session.term_signal', bool)
    cause = 'window_close' if closed else 'shell_eof' if eof else 'signal' if signaled else 'deadline'
    equal(result, 'session.stop_cause', cause)
    for key, value in dict(version=1, profile=PROFILE, session_nonce=nonce,
                           instance=app['instance'], slot=app['slot'], shell_pid=shell,
                           client_exit_status=0, foreground_pgid=shell, error='none').items():
        equal(result, 'startup.' + key, value)
    for key in ('client_record_complete', 'client_success', 'shell_return_observed',
                'client_reaped_by_shell', 'fallback_prompt_observed', 'foreground_matches_shell', 'complete'):
        equal(result, 'startup.' + key, True)
    ready = strict_json(raw('child_ready')); child = strict_json(raw('child'))
    for doc in (ready, child):
        equal(doc, 'parent_pid', app['pid']); equal(doc, 'child_pid', shell)
    for key, value in dict(sid=shell, pgid=shell, session_and_ctty_checked_in_child=True,
                           exec_status_pipe_closed=True, shell='/bin/sh').items():
        equal(ready, key, value)
    need(exec_boundary(ready, 'exec_boundary') == bound, 'child_exec_bound')
    equal(child, 'setup_checked', False)
    equal(child, 'job_control', 'disabled; foreground group must remain owned shell')
    flow = client_record(raw('client'), nonce, app, shell, client)
    need(raw('client_pid') == f'{client}\n'.encode(), 'client_pid_file')
    expected_return = pty_markers(raw('pty_rx'), nonce, app['instance'], shell, client)
    need(raw('shell_return') == expected_return, 'shell_return_file')
    grid_shape(raw('grid'))
    need(raw('stdout') == b'' and raw('stderr') == b'', 'app_nonempty_error_logs')
    status = (f'CHONKTERM/1 READY {app["instance"]} {app["pid"]}\n'
              f'CHONKTERM/1 CLEANUP {app["instance"]} {app["pid"]} 1 {app["shell_wait"]} {int(app["shell_term"])} {int(app["shell_kill"])}\n').encode()
    need(len(status) == app['status_bytes'], 'status_frame_bytes')
    return flow


def closed_records(records, extras, nonce, frontend_pid):
    front = frontend_closure(extras, frontend_pid)
    session = session_text(records['session_result'], nonce)
    manager_text(records['manager_status'], session, nonce)
    flows = [app_records(records, nonce, app) for app in session['apps']]
    pids = [front['pid']] + [flow[key] for flow in flows for key in ('app_pid', 'shell_pid', 'client_pid')]
    need(len(set(pids)) == 7, 'role_pid_alias')
    need(flows[0]['tuple'] != flows[1]['tuple'], 'flow_tuple_alias')
    return front, session, flows


def producer_seal(console, nonce, front, flows):
    """All raw bytes precede the C producer's final stability recheck.

    Its completion seal is therefore needed for those filesystem-only facts;
    the independent Python record predicates above remain mandatory.
    """
    seal = None; seal_line = None; last_observation = 0
    console_lines = console.split(b'\n')
    for index, raw in enumerate(console_lines):
        if b'DL30_OBSERVE_' in raw:
            last_observation = index
        if b'DL30_RESULT_' not in raw:
            continue
        raw = raw.rstrip(b'\r')  # Normalize line ending only; never interior bytes.
        need(index < len(console_lines) - 1, 'producer_seal_missing_LF')
        match = re.fullmatch(rb'DL30_RESULT_([0-9a-f]{48}) ([ -~]+)', raw)
        need(match is not None and match[1].decode() == nonce, 'producer_seal_nonce_or_syntax')
        need(seal is None, 'producer_seal_duplicate')
        seal = {}; seal_line = index
        for part in match[2].decode().split(' '):
            key, delimiter, value = part.partition('=')
            need(delimiter and key not in seal and value, 'producer_seal_key')
            if key in ('status', 'error'):
                need(re.fullmatch('[A-Za-z0-9_]+', value) is not None, 'producer_seal_text')
                seal[key] = value
            else:
                seal[key] = decimal(value)
    need(seal is not None and seal_line > last_observation, 'producer_seal_missing_or_order')
    keys = ('status desktop_ok frames_out frames_in packets_out packets_in error errno console_bytes_in partial_line partial_bits '
            'daemon_cleanup frontend_pid mdns_pid app1_pid app2_pid shell1_pid shell2_pid client1_pid client2_pid '
            'cleanup_complete phases if_error if_stage default_add_attempted route_error route_stage '
            'close_started_ms close_deadline_ms close_completed_ms finished_ms').split()
    need(set(seal) == set(keys), 'producer_seal_fields')
    need(seal['status'] in ('PASS', 'FAILED') and seal['desktop_ok'] == 1, 'producer_closed_recheck_failed')
    need(seal['frontend_pid'] == front['pid'], 'producer_frontend_pid')
    for flow in flows:
        for role in ('app', 'shell', 'client'):
            need(seal[f'{role}{flow["slot"]}_pid'] == flow[f'{role}_pid'], 'producer_role_pid', role)
    return {'desktop_ok': 1, 'frontend_pid': front['pid'], 'producer_status_preserved': seal['status'],
            'producer_error_preserved': seal['error'],
            'scope': 'Completion of source-bound final file stability checks only; producer status is not an independently qualified overall runtime result'}


def backend_correspondence(path, nonce, flows):
    """This revision does not bind/grade backend packets or forwarding."""
    data = load_file(Path(path), 1024 * 1024)
    record = strict_json(data, 1024 * 1024, integers_only=False)
    equal(record, 'schema', 1); equal(record, 'profile', PROFILE); equal(record, 'nonce', nonce)
    return {'status': 'NOT_EVALUATED', 'backend_result_sha256': digest(data),
            'reason': 'Backend2/TCP4 packet evidence is not bound or graded by this qualifier revision; requires separate raw Ethernet/relay packet checks and source-bound causal guard evidence, not policy_accepted/forwarded flags alone',
            'client_tuples_for_later_join': [{'slot': f['slot'], 'client_pid': f['client_pid'], 'tuple': f['tuple'],
                'request_sha256': digest(f['request']), 'response_sha256': digest(f['response'])} for f in flows],
            'wire_correspondence_claimed': False}


def qualify(console, nonce, frontend_pid=None, backend_result=None):
    report = {'schema_version': 1, 'scope': 'Independent offline closed desktop observations only',
              'closed_desktop': {'status': 'FAILED'}, 'backend_correspondence': {'status': 'NOT_EVALUATED'},
              'overall_runtime_PASS_claimed': False, 'GPU_observer_hold': True}
    try:
        nonce_valid(nonce)
        report['nonce'] = nonce
        report['console_sha256'] = digest(console)
        report['source_bindings'] = source_bindings()
        records, extras, observations = observe_records(console, nonce)
        report['observations'] = observations
        front, session, flows = closed_records(records, extras, nonce, frontend_pid)
        seal = producer_seal(console, nonce, front, flows)
        public_flows = [{key: value for key, value in flow.items() if key not in ('request', 'response')} |
                        {'request_sha256': digest(flow['request']), 'response_sha256': digest(flow['response']),
                         'request_bytes': len(flow['request']), 'response_bytes': len(flow['response'])}
                        for flow in flows]
        report['closed_desktop'] = {'status': 'VERIFIED_CLOSED_RECORDS', 'frontend_observed_closure': front,
                                    'session': session, 'clients': public_flows, 'producer_closed_record_seal': seal,
                                    'limits': ['Console observations are evidence, not independent proof against a malicious producer',
                                               'No filesystem metadata/closure is inferred from byte contents alone; source-bound producer and frontend wait/EOF observations supply that premise',
                                               'Normal client return is source-bound foreground shell evidence, not a waitpid on a grandchild',
                                               'No rendering pixels, physical typing, whole service lifecycle or Linux recovery qualification']}
        if backend_result is not None:
            try:
                report['backend_correspondence'] = backend_correspondence(backend_result, nonce, flows)
            except (Failure, OSError, ValueError, TypeError, KeyError, IndexError, RecursionError) as error:
                report['backend_correspondence'] = {'status': 'FAILED', 'error': str(error)[:300]}
        else:
            report['backend_correspondence']['reason'] = 'No backend result supplied; no wire correspondence claim'
    except (Failure, OSError, ValueError, TypeError, KeyError, IndexError, RecursionError) as error:
        report['closed_desktop'] = {'status': 'FAILED', 'error': str(error)[:300]}
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--console', type=Path, required=True)
    parser.add_argument('--nonce', required=True)
    parser.add_argument('--frontend-pid', type=int)
    parser.add_argument('--backend-result', type=Path)
    args = parser.parse_args(argv)
    try:
        console = load_file(args.console, MAX_CONSOLE)
        report = qualify(console, args.nonce, args.frontend_pid, args.backend_result)
    except (Failure, OSError, ValueError, TypeError) as error:
        report = {'schema_version': 1, 'scope': 'Independent offline closed desktop observations only',
                  'closed_desktop': {'status': 'FAILED', 'error': str(error)[:300]},
                  'backend_correspondence': {'status': 'NOT_EVALUATED'}, 'overall_runtime_PASS_claimed': False}
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report['closed_desktop']['status'] == 'VERIFIED_CLOSED_RECORDS' and report['backend_correspondence']['status'] != 'FAILED' else 1


if __name__ == '__main__':
    raise SystemExit(main())
