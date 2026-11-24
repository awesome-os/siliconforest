#!/usr/bin/env python3
"""Offline independent qualifier corpus and targeted decision mutations."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import types
sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
import qualify as q
import test_evidence as fixture


def main():
    tested = subprocess.run([sys.executable, '-B', str(HERE / 'test_evidence.py')], capture_output=True, text=True, timeout=60)
    if tested.returncode:
        raise RuntimeError(tested.stdout + tested.stderr)
    summary = json.loads(tested.stdout.splitlines()[-1])
    records = fixture.fixture_records(); normal = fixture.console(records); cases = {}
    data = dict(records); data['app1_client'] += b' '
    cases['digest'] = fixture.console(data).replace(hashlib.sha256(data['app1_client']).hexdigest().encode(), hashlib.sha256(records['app1_client']).hexdigest().encode())
    cases['producer_seal'] = normal.replace(b'desktop_ok=1', b'desktop_ok=0')
    cases['foreign_nonce'] = normal.replace(b'DL30_OBSERVE_' + fixture.NONCE.encode(), b'DL30_OBSERVE_' + b'f' * 48, 1)
    cases['offset'] = normal.replace(b'offset=64 ', b'offset=65 ', 1)
    _, extras, _ = q.observe_records(normal, fixture.NONCE)
    cases['frontend_wait'] = fixture.console(records, [('frontend_child', extras['frontend_child'][0].replace(b'wait_status=0', b'wait_status=9')), ('frontend_child_spawn', extras['frontend_child_spawn'][0])])
    cases['normal_return'] = fixture.console(fixture.altered(records, 'app1_result', ('startup', 'client_exit_status'), 1))
    cases['selected'] = fixture.console(fixture.altered(records, 'app1_client', ('selected',), '93.184.216.35'))
    cases['request'] = fixture.console(fixture.altered(records, 'app1_client', ('request_hex',), '00' * 186))
    doc = json.loads(records['app1_client']); raw = bytes.fromhex(doc['response_hex']).replace(b'Content-Length: 14', b'Content-Length: 15')
    doc['response_hex'] = raw.hex(); data = dict(records); data['app1_client'] = json.dumps(doc).encode()
    cases['HTTP_length'] = fixture.console(data)
    cases['tuple'] = fixture.console(fixture.altered(records, 'app2_client', ('local', 'port'), 50001))
    # The second request alone moves to15 (after PidOwned14, before AppReady20).
    data = dict(records); data['session_result'] = data['session_result'].replace(b'created_ms=30', b'created_ms=15')
    lines = data['manager_status'].splitlines(keepends=True)
    line = next(x for x in lines if b'attempt=1 ms=30 request=' in x)
    lines.remove(line); line = line.replace(b'ms=30 ', b'ms=15 ')
    before = next(i for i, x in enumerate(lines) if b'ms=20 event=AppReady' in x)
    lines.insert(before, line); data['manager_status'] = b''.join(lines)
    cases['serialized_start'] = fixture.console(data)
    mutations = [
        ('producer_seal', "seal['desktop_ok'] == 1", 'True'),
        ('digest', "len(proof) == 32 and proof.hex() == digest(records[kind])", 'True'),
        ('foreign_nonce', 'got_nonce.decode() == nonce', 'True'),
        ('offset', 'offset == len(data)', 'True'),
        ('frontend_wait', 'row[key] == value', "key == 'wait_status' or row[key] == value"),
        ('normal_return', 'field(obj, path, type(expected)) == expected', "path == 'startup.client_exit_status' or field(obj, path, type(expected)) == expected"),
        ('selected', 'field(obj, path, type(expected)) == expected', "path == 'selected' or field(obj, path, type(expected)) == expected"),
        ('request', 'request == request_bytes(nonce)', 'True'),
        ('HTTP_length', 'len(body) == int(count)', 'True'),
        ('tuple', "flows[0]['tuple'] != flows[1]['tuple']", 'True'),
        ('serialized_start', "a == 0 or 'AppReady' in seen[0]", 'True'),
    ]
    source = (HERE / 'qualify.py').read_text(); results = []
    for name, before, after in mutations:
        if source.count(before) != 1:
            raise RuntimeError('Mutation anchor ' + name)
        normal_negative = q.qualify(cases[name], fixture.NONCE)
        if normal_negative['closed_desktop']['status'] != 'FAILED':
            raise RuntimeError('Invalid negative fixture ' + name)
        module = types.ModuleType('independent_qualifier_mutant_' + name)
        module.__file__ = str(HERE / 'qualify.py')
        exec(compile(source.replace(before, after), module.__file__, 'exec'), module.__dict__)
        if module.qualify(normal, fixture.NONCE)['closed_desktop']['status'] != 'VERIFIED_CLOSED_RECORDS':
            raise RuntimeError('Mutation broke golden fixture ' + name)
        mutant_result = module.qualify(cases[name], fixture.NONCE)
        if mutant_result['closed_desktop']['status'] != 'VERIFIED_CLOSED_RECORDS':
            raise RuntimeError('Mutant not reached/detected by negative fixture ' + name + ': ' + str(mutant_result['closed_desktop']))
        results.append({'name': name, 'compiled_original_Python': True, 'golden_preserved': True,
                        'negative_corpus_detects_incorrect_acceptance': True,
                        'original_failure': normal_negative['closed_desktop']['error']})
    report = {'scope': 'Source-only independent Python qualifier; synthetic console/records and host filesystem; no C proof invocation or target actions',
              'corpus': summary, 'mutants': results, 'mutant_count': len(results),
              'qualifier_sha256': q.digest((HERE / 'qualify.py').read_bytes()),
              'test_sha256': q.digest((HERE / 'test_evidence.py').read_bytes()),
              'runner_sha256': q.digest(Path(__file__).read_bytes()),
              'golden_console_sha256': q.digest(normal),
              'golden_records': {k: q.digest(v) for k, v in records.items()},
              'source_bindings': q.source_bindings(),
              'backend_correspondence': 'NOT_EVALUATED; TCP4/backend2 packet chronology not bound or regraded',
              'actual_runtime_claims': False}
    (HERE / 'closed-validation-001.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'corpus': summary, 'mutants_detected': len(results),
                      'validation_sha256': q.digest((HERE / 'closed-validation-001.json').read_bytes())}))


if __name__ == '__main__':
    main()
