#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Use the shared hold parser, including actual bare core-lock releases.

Some owners record `core.lock released` without an install-result suffix. That
ends the active pipeline regardless of its success. It says nothing about a
later reservation or any other owner's hold. Never invent completion events.
"""
import argparse
import importlib.util
from pathlib import Path
import re


def completed_core_starts(text):
    lines = text.splitlines()
    active = {}
    retired = set()
    for index, line in enumerate(lines):
        event = re.match(r'^\S+\s+([A-Z][A-Z0-9_-]*)[: ]+(.+)$', line)
        if not event:
            continue
        owner, body = event[1], event[2].lower()
        if re.match(r'^(?:core\.lock:\s*)?guarded(?: core)? install\b', body):
            active[owner] = index
        elif (body == 'core.lock released' or re.fullmatch(r'core\.lock released \(guarded install rc=-?\d+\)', body)) and owner in active:
            retired.add(active.pop(owner))
    return '\n'.join(line for index, line in enumerate(lines) if index not in retired)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--max-seconds', type=int, help='Hard bound for an explicitly offered pre-reboot window')
    args = ap.parse_args()
    if args.max_seconds is not None and not 1 <= args.max_seconds <= 600:
        ap.error('--max-seconds must be between 1 and 600')
    repo = Path(__file__).resolve().parents[3]
    spec = importlib.util.spec_from_file_location('shared_coord_gate',
        repo / 'integrations/siliconforest/finch/tools/coord-gate.py')
    shared = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(shared)
    source = (repo / 'runs/m1-coord/log.txt').read_text()
    pending = shared.pending_hold(completed_core_starts(source), max_seconds=args.max_seconds)
    if pending:
        print('M1 SSH hold active; no connection attempted. ' + pending)
        return 77
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
