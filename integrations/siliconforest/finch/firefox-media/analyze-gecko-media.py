#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""CPU-weight a saved raw Gecko profile using identity-matched Breakpad FUNCs.

Offline only. Selected threads and sampled stacks are not whole-process cost,
proof that a wait spins, uninstrumented speed or visible playback evidence.
"""
import argparse
import bisect
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import re


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def symbols(path):
    rows = []
    with path.open() as stream:
        header = stream.readline().strip().split(None, 4)
        if len(header) != 5 or header[:3] != ['MODULE', 'Mac', 'arm64']:
            raise ValueError('Require an arm64 Mac Breakpad module header')
        for line in stream:
            if not line.startswith('FUNC '):
                continue
            fields = line.split(None, 5 if line.startswith('FUNC m ') else 4)
            if fields[1] == 'm':
                fields.pop(1)
            rows.append((int(fields[1], 16), int(fields[2], 16), fields[4].rstrip()))
    rows.sort()
    return header[4], dict(module=header[3], rows=rows, starts=[r[0] for r in rows],
                          path=str(path), sha256=digest(path))


def analyze(profile, tables, roles, top_labels=20):
    if not 1 <= top_labels <= 1000:
        raise ValueError('Output label limit must be1..1000')
    output = []
    for process in [profile, *profile.get('processes', [])]:
        threads = process.get('threads', [])
        if not threads or threads[0].get('processType') not in roles:
            continue
        meta = process['meta']
        if meta.get('sampleUnits', profile['meta'].get('sampleUnits', {})).get('threadCPUDelta') != 'µs':
            raise ValueError('Unknown CPU-delta units; no wall-time substitution')
        window_us = (meta['profilingEndTime'] - meta['profilingStartTime']) * 1000
        if window_us <= 0:
            raise ValueError('Incomplete profile interval')
        libraries = sorted(process.get('libs', []), key=lambda lib: lib['start'])
        starts = [lib['start'] for lib in libraries]
        for lib in libraries:
            table = tables.get(lib.get('name'))
            if table and lib.get('breakpadId', '').replace('-', '').upper() != table['module'].upper():
                raise ValueError('Symbol identity mismatch: ' + lib['name'])
        locations = {}

        def resolve(location):
            if location in locations:
                return locations[location]
            if not re.fullmatch('0x[0-9a-fA-F]+', location):
                return location
            pc = int(location, 16)
            index = bisect.bisect_right(starts, pc) - 1
            result = location
            if index >= 0 and pc < libraries[index]['end']:
                lib = libraries[index]
                offset = pc - lib['start'] + lib.get('offset', 0)
                result = lib['name'] + '+0x%x' % offset
                table = tables.get(lib['name'])
                if table:
                    i = bisect.bisect_right(table['starts'], offset) - 1
                    if i >= 0 and offset < table['rows'][i][0] + table['rows'][i][1]:
                        result = lib['name'] + '!' + table['rows'][i][2]
            locations[location] = result
            return result

        for thread in threads:
            samples = thread['samples']
            cpu_column = samples['schema'].get('threadCPUDelta')
            if cpu_column is None:
                output.append(dict(role=thread['processType'], thread=thread['name'], cpu_unavailable=True))
                continue
            stack_column = samples['schema']['stack']
            strings = thread.get('stringTable', thread.get('stringArray', []))
            stack_schema, frame_schema = thread['stackTable']['schema'], thread['frameTable']['schema']
            stacks, leaf, inclusive = {}, Counter(), Counter()
            total, missing, unstacked = 0, 0, 0
            for row in samples['data']:
                if len(row) <= cpu_column or row[cpu_column] is None:
                    missing += 1
                    continue
                weight = row[cpu_column]
                if not isinstance(weight, (int, float)) or not math.isfinite(weight) or weight < 0:
                    raise ValueError('Invalid CPU delta')
                total += weight
                sid = row[stack_column]
                if not weight:
                    continue
                if sid is None:
                    unstacked += weight
                    continue
                if sid not in stacks:
                    labels, seen, current = [], set(), sid
                    while current is not None:
                        if current in seen:
                            raise ValueError('Cycle in saved stack table')
                        seen.add(current)
                        stack = thread['stackTable']['data'][current]
                        frame = thread['frameTable']['data'][stack[stack_schema['frame']]]
                        labels.append(resolve(strings[frame[frame_schema['location']]]))
                        current = stack[stack_schema['prefix']]
                    stacks[sid] = labels
                labels = stacks[sid]
                if labels:
                    leaf[labels[0]] += weight
                for label in set(labels):
                    inclusive[label] += weight
            def top(counter):
                return [dict(name=name, cpu_ms=value / 1000,
                             percent_observed_thread_cpu=value / total * 100)
                        for name, value in counter.most_common(top_labels)] if total else []
            output.append(dict(role=thread['processType'], pid=thread['pid'], tid=thread['tid'],
                thread=thread['name'], cpu_ms=total / 1000, percent_one_core=total / window_us * 100,
                sample_count=len(samples['data']), missing_cpu_delta_rows=missing,
                unstacked_cpu_ms=unstacked / 1000, leaf=top(leaf), inclusive=top(inclusive),
                decoder_labels=sorted({s for s in strings if s.startswith('DecodeFrame ')})))
    return dict(threads=sorted(output, key=lambda row: row.get('cpu_ms', -1), reverse=True),
        diagnostic_only=True, complete_process_cost=False,
        limits='Selected sampled threads only. CPU deltas describe the interval before a stack sample; a wait leaf is not proof of spinning. Unresolved native offsets remain unresolved. No visible playback or unprofiled speed qualification.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', type=Path, required=True)
    parser.add_argument('--symbols', type=Path, action='append', required=True)
    parser.add_argument('--roles', nargs='+', default=['default', 'rdd', 'gpu'])
    parser.add_argument('--top', type=int, default=20, choices=range(1,1001), metavar='1..1000',
                        help='Output labels per thread; raise for codec identity without changing weights')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    tables = {}
    for path in args.symbols:
        name, table = symbols(path)
        if name in tables:
            raise ValueError('Duplicate module symbols: ' + name)
        tables[name] = table
    result = analyze(json.loads(args.profile.read_text()), tables, args.roles, args.top)
    result.update(profile=str(args.profile), profile_sha256=digest(args.profile),
                  symbols={name: {key: value for key, value in table.items() if key not in ['rows', 'starts']}
                           for name, table in tables.items()})
    args.output.write_text(json.dumps(result, indent=2) + '\n')
