#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Kearney and the SiliconForest contributors
"""Run tools/check.py on a source-only checkout (CI) and judge its report.

tools/check.py is strict on purpose: any error or skip makes it FAILED_OR_INCOMPLETE.
One of its tests reads a boot packet and an m1n1 checkout that are build artefacts and
are not in git, so on a clone it can only end in FileNotFoundError. This wrapper runs
check.py unchanged and accepts its result only if that one known test is the sole thing
that did not pass and the reason is the missing artefact. It reports the test as NOT RUN;
it never calls the run a full pass. With the artefacts present, use tools/check.py itself.
"""
import json, re, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NEEDS_ARTEFACT = {
    'test_m1_kernel_cycle.KernelCycleTests.test_real_offline_source_and_packet_verification':
        'artifacts/m1-firstboot-007-r6/manifest.json',
}

def main(argv):
    report = ROOT / '.build/checks.json'
    rc = subprocess.run([sys.executable, str(ROOT / 'tools/check.py'), '--report', str(report), *argv]).returncode
    data = json.loads(report.read_text())
    if rc == 0 and data['status'] == 'PASSED_LOCAL_TOOLING':
        print('ci_check: tools/check.py passed in full'); return 0
    unit = data['unit_tests']
    log = report.with_suffix('.unittest.txt').read_text()
    errored = set(re.findall(r'^ERROR: \S+ \((\S+)\)$', log, re.M))
    problems = []
    if unit['failures'] or unit['skipped']:
        problems.append(f"unit tests: {unit['failures']} failure(s), {unit['skipped']} skip(s)")
    if errored != set(NEEDS_ARTEFACT) or unit['errors'] != len(NEEDS_ARTEFACT):
        problems.append(f'unexpected test errors: {sorted(errored - set(NEEDS_ARTEFACT))} ({unit["errors"]} in all)')
    for test, path in NEEDS_ARTEFACT.items():
        if (ROOT / path).exists():
            problems.append(f'{test} errored although {path} exists')
        elif test in errored and not re.search(r'FileNotFoundError: .*' + re.escape(path), log):
            problems.append(f'{test} errored for another reason than the missing {path}')
    suites, commands = data['native_c_suites'], data['c_commands']
    if len(suites) != 5 or not all(s['passed_groups'] > 0 for s in suites) or not all(c['returncode'] == 0 for c in commands):
        problems.append('a native C suite did not pass')
    if problems:
        print('ci_check: FAILED:', *problems, sep='\n  '); return 1
    print(f"ci_check: {unit['run'] - len(NEEDS_ARTEFACT)} of {unit['run']} unit tests passed, "
          f"{data['native_c_test_groups']} C test groups passed.")
    for test, path in NEEDS_ARTEFACT.items():
        print(f'ci_check: NOT RUN (needs untracked {path}): {test}')
    print('ci_check: accepted for a source-only checkout; this is not a full pass of tools/check.py')
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
