#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Extract Acis DPE only; Apple inputs, output and provenance stay in private/ave/."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
from datetime import datetime, timezone


def private_path(root, value):
    path = Path(value).resolve()
    if not path.is_relative_to(root.resolve()) or path == root.resolve():
        raise ValueError('Apple inputs/outputs must remain under ignored private/ave/')
    return path


def module():
    spec = importlib.util.spec_from_file_location('ave_fwgen', Path(__file__).parent/'upstream/ave_fwgen.py')
    tool = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tool)
    return tool


def extract(source, tool):
    decoded = tool.kernelcache_macho(source)
    kc = tool.Kernelcache(decoded)
    tables = tool.dpe_tables(kc, 'Acis_8103')
    counts = [len(tables[key]) for key, _ in tool.DPE_SLOTS]
    if counts != [0, 1, 39]:
        raise ValueError('Acis table layout differs from reviewed 13.5 implementation')
    return tool.dpe_encode(tables), counts, hashlib.sha256(decoded).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--kernelcache', type=Path, required=True)
    ap.add_argument('--source', required=True, help='Source URL or read-only local origin, PRIVATE ledger only')
    ap.add_argument('--build', required=True, help='Apple OS build, PRIVATE ledger only')
    ap.add_argument('--os-version', required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--metadata', type=Path, required=True, help='Private live ADT metadata')
    a = ap.parse_args()
    repo = Path(__file__).resolve().parents[4]
    private = repo/'private/ave'
    paths = [private_path(private, value) for value in [a.kernelcache, a.output, a.metadata]]
    source, output, metadata = paths
    # This first extractor is reviewed only for the exact 13.5 table/ABI.
    # A newer host OS is not evidence of a different preload version.
    if a.os_version != '13.5':
        raise ValueError('Only reviewed 13.5 extraction supported; analyze other builds first')
    measured = json.loads(metadata.read_text())
    chosen = next((row for row in measured if row.get('path') == 'IODeviceTree:/chosen'), {})
    ave = next((row for row in measured if row.get('path') == 'IODeviceTree:/arm-io/ave'), {})
    props = ave.get('properties', {})
    sizes = [s['size'] for s in props.get('segment-ranges', {}).get('segments', [])]
    if chosen.get('properties', {}).get('firmware-version') != 'iBoot-8422.141.2' or sizes != [0xcc000, 0x128000]:
        raise ValueError('Live metadata does not identify reviewed 13.5 preload layout')
    if props.get('pre-loaded') not in ('\x01', 1, True):
        raise ValueError('Live metadata does not mark AVE preloaded')
    for path in [source, metadata, output.parent]:
        if subprocess.run(['git','check-ignore','--quiet',str(path)], cwd=repo).returncode:
            raise ValueError('Private path must be git-ignored')
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        raise ValueError('Refusing to overwrite extracted output')
    image = source.read_bytes()
    payload, counts, decoded_hash = extract(image, module())
    ledger = private/'ledger.json'
    records = json.loads(ledger.read_text()) if ledger.exists() else []
    records.append({'time':datetime.now(timezone.utc).isoformat(), 'operation':'extract-acis-dpe',
                    'source':a.source,'os_version':a.os_version,'build':a.build,
                    'inputs':[{'path':str(p.relative_to(repo)), 'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in [source, metadata]],
                    'decoded_kernelcache_sha256':decoded_hash,
                    'output':str(output.relative_to(repo)), 'output_sha256':hashlib.sha256(payload).hexdigest(),
                    'upstream_commit':'9ab14e45281e8f61fb499b760408028171a5f2b5',
                    'counts':counts,'compatibility':'layout only; firmware TEXT identity and reserved cold DATA still required before hardware use'})
    # Write ledger first so extracted bytes never appear without provenance.
    ledger.parent.mkdir(parents=True, exist_ok=True)
    with ledger.open('w') as f:json.dump(records,f,indent=2);f.write('\n')
    with output.open('xb') as f:f.write(payload)
    print('Acis DPE extracted privately; provenance recorded in private ledger. No firmware file extracted.')


if __name__ == '__main__':
    main()
