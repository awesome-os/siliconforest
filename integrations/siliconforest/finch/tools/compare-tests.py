#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
"""CLI-only port of Finch tools/host-tests.sh's comparison policy.

Capture Linux-built programs separately on SiliconForest and the reference Mac;
compare their receipts here. Never invokes SSH, a build tool or a GUI test.
Finch's masks and exceptions come from the same pinned source as the programs.
"""
import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import resource
import signal
import subprocess
import sys
import time


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def capture(args):
    root = args.manifest.resolve().parent
    manifest = json.loads(args.manifest.read_text())
    if args.role == 'reference' and not args.names:
        raise ValueError('Reference capture requires explicitly reviewed test names')
    records = []
    for test in manifest:
        if args.names and test['name'] not in args.names:
            continue
        if args.role == 'reference' and test['mode'] != 'reference':
            raise ValueError('This test has no reference mode: '+test['name'])
        if test['mode'] not in ('reference', 'expected', 'exit'):
            raise ValueError('Unknown comparison mode: '+test['mode'])
        if test.get('gui', False):
            raise ValueError('GUI entries are outside this harness: '+test['name'])
        argv = [str(root / test['argv'][0]), *test['argv'][1:]]
        env = os.environ.copy()
        # Reference runs select the controller's libraries, not a caller's trial.
        for key in list(env):
            if key.startswith('DYLD_'):
                del env[key]
        if args.role == 'candidate':
            env.update({k: v.replace('@ROOT@', str(root)) for k, v in test.get('env', {}).items()})
        record = dict(name=test['name'], role=args.role, mode=test['mode'],
                      skip_lines=test.get('skip_lines', 0), stdout='', stderr='')
        start = time.monotonic()
        try:
            record['sha256'] = hashlib.sha256(Path(argv[0]).read_bytes()).hexdigest()
            proc = subprocess.Popen(argv, cwd=root, env=env, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, start_new_session=True,
                                    preexec_fn=no_core)
            try:
                out, err = proc.communicate(timeout=test.get('timeout', 60))
                record['status'] = 'OK' if proc.returncode == 0 else 'FAIL'
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                out, err = proc.communicate()
                record['status'] = 'TIMEOUT'
            record.update(returncode=proc.returncode,
                          stdout=out.decode('utf-8', 'replace'),
                          stderr=err.decode('utf-8', 'replace'))
        except OSError as error:
            record.update(status='BLOCKED', error=str(error))
        record['seconds'] = round(time.monotonic()-start, 3)
        if test['mode'] == 'expected':
            record['expected'] = (root/test['expected']).read_text()
        records.append(record)
        args.output.write_text(json.dumps(records, indent=2)+'\n')
        print(f"{record['name']}: {record['status']}", flush=True)
    known = {t['name'] for t in manifest}
    if set(args.names)-known:
        raise ValueError('Unknown tests: '+', '.join(sorted(set(args.names)-known)))
    if not records:
        raise ValueError('No tests selected')
    return int(any(r['status'] != 'OK' for r in records))


def masked(name, text, masks):
    # Run the pinned sed -E substitutions exactly as Finch does. The masks are
    # data from the pinned checkout, never shell input; no regex translation.
    expressions = []
    for line in masks.read_text().splitlines():
        if not line.strip() or line.startswith('#'):
            continue
        test, expression = line.split(None, 1)
        if test == name:
            expressions.extend(['-e', expression])
    if not expressions:
        return text
    return subprocess.run(['sed', '-E', *expressions], input=text, text=True,
                          capture_output=True, check=True).stdout


def compare(args):
    candidate = json.loads(args.candidate.read_text())
    reference = {r['name']: r for r in json.loads(args.reference.read_text())} if args.reference else {}
    exceptions = {}
    for line in args.exceptions.read_text().splitlines():
        if line.strip() and not line.startswith('#'):
            name, reason = line.split(':', 1)
            # Display exceptions are outside this CLI-only harness.
            if '(1x display)' not in name:
                exceptions[name] = reason.strip()
    results = []
    for row in candidate:
        name = row['name']
        result = dict(name=name, status='FAIL')
        if row.get('role') != 'candidate' or row['status'] != 'OK':
            result['reason'] = 'Candidate did not complete successfully'
        elif row['mode'] == 'exit':
            result.update(status='EXIT_OK', reason='Program assertions only; no reference comparison')
        else:
            ref = reference.get(name)
            if row['mode'] == 'expected':
                left = row['expected']
            elif ref is None:
                result.update(status='BLOCKED', reason='No controller-Mac receipt')
                left = None
            elif ref.get('role') != 'reference' or ref['status'] != 'OK':
                result['reason'] = 'Reference did not complete successfully'
                left = None
            elif ref.get('sha256') != row.get('sha256'):
                result['reason'] = 'Reference and candidate executed different binary bytes'
                left = None
            else:
                left = ref['stdout']
            if left is not None:
                skip = row.get('skip_lines', 0) if row['mode'] == 'reference' else 0
                left = ''.join(left.splitlines(keepends=True)[skip:])
                right = ''.join(row['stdout'].splitlines(keepends=True)[skip:])
                left, right = masked(name, left, args.masks), masked(name, right, args.masks)
                if not left:
                    result['reason'] = 'Empty expected/reference output'
                elif left == right:
                    result['status'] = 'MATCH'
                else:
                    result.update(status='DIFF', diff=''.join(difflib.unified_diff(
                        left.splitlines(True), right.splitlines(True), 'reference', 'candidate')))
            if name in exceptions and result['status'] in ('MATCH', 'DIFF'):
                result.update(status='XPASS' if result['status']=='MATCH' else 'XFAIL',
                              reason=exceptions[name])
        results.append(result)
        print(f"{name}: {result['status']}")
    if not results:
        raise ValueError('Empty candidate receipt')
    args.output.write_text(json.dumps(results, indent=2)+'\n')
    # Known differences remain failures; an expected failure is never a pass.
    return int(any(r['status'] not in ('MATCH', 'EXIT_OK') for r in results))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    run = sub.add_parser('capture')
    run.add_argument('manifest', type=Path)
    run.add_argument('--role', required=True, choices=['candidate', 'reference'])
    run.add_argument('--output', required=True, type=Path)
    run.add_argument('names', nargs='*')
    diff = sub.add_parser('compare')
    diff.add_argument('candidate', type=Path)
    diff.add_argument('--reference', type=Path)
    diff.add_argument('--masks', type=Path, required=True)
    diff.add_argument('--exceptions', type=Path, required=True)
    diff.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    return capture(args) if args.command == 'capture' else compare(args)


if __name__ == '__main__':
    sys.exit(main())
