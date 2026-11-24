#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare a conformance program's JSON from SiliconForest with the reference from macOS.

  compare.py REFERENCE.json OURS.json [--tolerance 0.01] [--out scorecard.md]
  compare.py --from-serial runs/cocoa/<run>/serial.log --name METRICS > ours.json
      (takes the JSON a guest program printed between CONF_<NAME>_BEGIN and CONF_<NAME>_END)

Every numeric leaf is geometry in points (or a colour component). A leaf is a mismatch when
it differs by more than the tolerance, missing when our side has null or lacks it. The
scorecard groups leaves by area (the first two path components), worst area first.
"""
import argparse
import collections
import json
import re
import sys


def leaves(node, path=()):
    if isinstance(node, dict):
        for k, v in node.items():
            yield from leaves(v, path + (k,))
    else:
        yield path, node


def from_serial(path, name):
    text = open(path, 'rb').read().decode(errors='replace')
    begin, end = text.rfind('CONF_%s_BEGIN' % name), text.rfind('CONF_%s_END' % name)
    if begin < 0 or end < begin:
        raise SystemExit('no CONF_%s block in %s' % (name, path))
    # the console may have other output mixed in before the object and wraps nothing itself
    body = text[begin:end].replace('\r', '')
    return json.loads(body[body.index('{'):body.rindex('}') + 1])


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('files', nargs='*')
    p.add_argument('--from-serial')
    p.add_argument('--name', default='METRICS')
    p.add_argument('--tolerance', type=float, default=0.01)
    p.add_argument('--out')
    a = p.parse_args()
    if a.from_serial:
        json.dump(from_serial(a.from_serial, a.name), sys.stdout, indent=1)
        print()
        return
    ref, ours = (json.load(open(f)) for f in a.files[:2])
    ours_leaves = dict(leaves(ours))
    areas = collections.defaultdict(lambda: dict(total=0, equal=0, mismatch=[], missing=[]))
    for path, want in leaves(ref):
        area = areas['/'.join(path[:2])]
        area['total'] += 1
        got = ours_leaves.get(path)
        if got is None:
            area['missing'].append(('/'.join(path[2:]), want))
        elif isinstance(want, (int, float)) and isinstance(got, (int, float)):
            if abs(got - want) <= a.tolerance:
                area['equal'] += 1
            else:
                area['mismatch'].append(('/'.join(path[2:]), want, got, got - want))
        elif want == got:
            area['equal'] += 1
        else:
            area['mismatch'].append(('/'.join(path[2:]), want, got, None))
    order = sorted(areas.items(), key=lambda kv: -(len(kv[1]['mismatch']) + len(kv[1]['missing'])) / max(kv[1]['total'], 1))
    total = sum(v['total'] for v in areas.values())
    equal = sum(v['equal'] for v in areas.values())
    lines = ['# Conformance scorecard: %s against %s' % (a.files[1], a.files[0]), '',
             '%d values compared, %d equal within %.3g, %d different, %d missing on our side.' % (
                 total, equal, a.tolerance, sum(len(v['mismatch']) for v in areas.values()), sum(len(v['missing']) for v in areas.values())), '',
             '| Area | Values | Equal | Different | Missing | Largest difference |', '|---|---|---|---|---|---|']
    for name, v in order:
        deltas = [abs(m[3]) for m in v['mismatch'] if m[3] is not None]
        lines.append('| %s | %d | %d | %d | %d | %s |' % (name, v['total'], v['equal'], len(v['mismatch']), len(v['missing']),
                                                        ('%.3f' % max(deltas)) if deltas else ''))
    lines += ['', '## Differences, worst area first', '']
    for name, v in order:
        if not v['mismatch'] and not v['missing']:
            continue
        lines += ['### %s' % name, '']
        for leaf, want, got, delta in sorted(v['mismatch'], key=lambda m: -(abs(m[3]) if m[3] is not None else 1e9))[:40]:
            lines.append('- `%s`: macOS %s, ours %s%s' % (leaf, want, got, '' if delta is None else ' (%+.3f)' % delta))
        if v['missing']:
            lines.append('- missing on our side: ' + ', '.join('`%s`' % m[0] for m in v['missing'][:30]) + (' ...' if len(v['missing']) > 30 else ''))
        lines.append('')
    text = '\n'.join(lines) + '\n'
    if a.out:
        open(a.out, 'w').write(text)
    print('\n'.join(lines[:3 + len(order) + 4]))


if __name__ == '__main__':
    main()
