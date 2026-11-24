#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""One table from several hb_qemu.py runs: per test, the result of the last run that ran it.

  hb_report.py r4 r5 r6 ...   (run names under runs/homebrew/qemu/, oldest first)
"""
import json
from pathlib import Path
import sys

RUNS = Path(__file__).resolve().parents[3] / 'runs/homebrew/qemu'


def main():
    latest, order = {}, []
    for run in sys.argv[1:]:
        for r in json.loads((RUNS / run / 'results.json').read_text())['results']:
            if r['status'] == 'NOT RUN':
                continue
            if r['test'] not in latest:
                order.append(r['test'])
            latest[r['test']] = dict(r, run=run)
    print('| Test | Formula | Kind | Result | Run | Failure, or output tail |')
    print('|---|---|---|---|---|---|')
    for t in order:
        r = latest[t]
        text = r['why'] if r['status'] != 'PASS' else ' '.join(r['output'].split())[-90:]
        print('| %s | %s | %s | %s | %s | `%s` |' % (t, r['formula'], r['kind'], r['status'], r['run'], text.replace('|', '\\|').replace('`', "'")[:230]))
    n = sum(r['status'] == 'PASS' for r in latest.values())
    print('\n%d of %d tests pass (%d real-use, %d version-only among the passes).' % (
        n, len(latest), sum(r['status'] == 'PASS' and r['kind'] == 'real' for r in latest.values()),
        sum(r['status'] == 'PASS' and r['kind'] == 'version' for r in latest.values())))


if __name__ == '__main__':
    main()
