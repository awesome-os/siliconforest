#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Select the qualified Firefox network backend; plan unless --apply.

Use under the shared GUI/core locks after notice and a fresh coordination gate.
The payload must be a separate, unexecuted copy from the original build output.
Existing owner processes and profile files are never changed.
"""
import argparse
import ast
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import uuid as uuid_module

ROOT = Path('/usr/local/lib/siliconforest/firefox/160-system1')
APP = Path('/Applications/Firefox.app')
DEST = ROOT / 'overrides/network-v1'
BACKUP = ROOT / 'maintenance-network-v1'

def sha(data):
    return hashlib.sha256(data).hexdigest()

def image_uuid(path):
    # Read metadata only: executed Mach-O pages may contain lowered PAC bytes.
    with path.open('rb') as stream:
        header = stream.read(32)
        if len(header) != 32 or struct.unpack_from('<I', header)[0] != 0xFEEDFACF:
            raise ValueError('Expected an arm64 Mach-O image')
        count, size = struct.unpack_from('<II', header, 16)
        if size > 1024 * 1024:
            raise ValueError('Oversized Mach-O load commands')
        commands = stream.read(size)
    at = 0
    for _ in range(count):
        if at + 8 > len(commands):
            raise ValueError('Truncated Mach-O command')
        kind, length = struct.unpack_from('<II', commands, at)
        if length < 8 or at + length > len(commands):
            raise ValueError('Invalid Mach-O command')
        if kind == 0x1B and length == 24:
            return str(uuid_module.UUID(bytes=commands[at+8:at+24])).upper()
        at += length
    raise ValueError('Missing Mach-O UUID')

def atomic(path, data, mode):
    tmp = path.with_name(path.name + '.network-tmp')
    with tmp.open('xb') as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    tmp.chmod(mode)
    os.replace(tmp, path)

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source', type=Path, required=True)
    ap.add_argument('--apply', action='store_true')
    ap.add_argument('--rollback', action='store_true')
    ap.add_argument('--retry', action='store_true', help='reselect an unchanged qualified image after checked rollback')
    args = ap.parse_args()
    if args.retry and args.rollback:
        ap.error('--retry and --rollback are mutually exclusive')
    source = args.source.resolve()
    if not source.is_relative_to('/private/var/db/sffx'):
        raise ValueError('Expected a new owned private staging directory')
    launcher = APP / 'Contents/MacOS/firefox-sf'
    release = ROOT / 'release.json'
    marker = json.loads((APP / 'Contents/siliconforest-install.json').read_text())
    installation = json.loads((ROOT / 'installation.json').read_text())
    if marker != {'runtime': str(ROOT), 'token': installation['token']}:
        raise ValueError('Application ownership changed')
    if args.rollback:
        receipt = json.loads((BACKUP / 'receipt.json').read_text())
        if sha(launcher.read_bytes()) != receipt['new_launcher_sha256'] or sha(release.read_bytes()) != receipt['new_release_sha256']:
            raise ValueError('Later installed changes must not be overwritten')
        print(json.dumps({'action': 'restore previous launcher and manifest', 'apply': args.apply}))
        if args.apply:
            atomic(launcher, (BACKUP / 'launcher').read_bytes(), receipt['launcher_mode'])
            atomic(release, (BACKUP / 'release.json').read_bytes(), receipt['release_mode'])
        return
    manifest = json.loads(release.read_bytes())
    if not (manifest.get('target') == str(ROOT) and manifest.get('system_layout') is True
            and manifest.get('system_ui') is True and manifest.get('system_gpu_providers') is True
            and not manifest.get('diagnostics') and not manifest.get('network_override')):
        raise ValueError('Expected the qualified canonical UI/graphics runtime')
    if not args.retry and (DEST.exists() or BACKUP.exists()):
        raise ValueError('Refuse an existing installation or rollback directory')
    cold = json.loads((source / 'cold-verified.json').read_text())
    expected = cold['sha256']
    # This source tree has never been executed; verify it again before copying.
    for name, digest in expected.items():
        if sha((source / name).read_bytes()) != digest:
            raise ValueError('Cold payload changed: ' + name)
    for name, target in cold['symlinks'].items():
        if not (source / name).is_symlink() or os.readlink(source / name) != target:
            raise ValueError('Cold payload link changed: ' + name)
    payload = source / 'Frameworks/SystemConfiguration.framework'
    if not (payload / 'Versions/A/SystemConfiguration').is_file():
        raise ValueError('Missing built framework')
    proof = json.loads((source / 'qualification.json').read_text())
    cases = proof['browser']['cases']
    candidates = [r for r in cases if r['mode'] == 'candidate']
    if len(candidates) < 3 or any(r.get('natural_quit_pass') is not True or r.get('cleanup_signals') for r in candidates):
        raise ValueError('Three natural Firefox quits without cleanup required')
    uuid = proof['uuid']
    for row in candidates:
        if not any('<'+uuid+'>' in line and line.endswith('/Frameworks/SystemConfiguration.framework/Versions/A/SystemConfiguration')
                   for line in row.get('sc_images', [])):
            raise ValueError('Qualified network image was not observed in the browser')
    native = (source / 'native-api.log').read_text()
    if 'backend_cycles=65 reach_callbacks=65 store_callbacks=65 retains=130 releases=130 descriptors=5 failures=0' not in native:
        raise ValueError('Native callback/context/descriptor controls failed')
    candidate = (source / 'daily-launch.py').read_bytes()
    ast.parse(candidate)
    if b"env['MOZ_CRASHREPORTER_DISABLE'] = '1'" not in candidate:
        raise ValueError('Reporter containment must stay enabled')
    prior_launcher, prior_release = launcher.read_bytes(), release.read_bytes()
    baseline = json.loads((source / 'baseline.json').read_text())
    if baseline != {'launcher_sha256': sha(prior_launcher), 'release_sha256': sha(prior_release)}:
        raise ValueError('Runtime changed since staging')
    manifest['network_override'] = str(DEST / 'Frameworks')
    new_release = (json.dumps(manifest, indent=2) + '\n').encode()
    receipt = {'installed_at': datetime.now(timezone.utc).isoformat(), 'uuid': uuid,
               'previous_launcher_sha256': sha(prior_launcher), 'previous_release_sha256': sha(prior_release),
               'new_launcher_sha256': sha(candidate), 'new_release_sha256': sha(new_release),
               'launcher_mode': launcher.stat().st_mode & 0o777, 'release_mode': release.stat().st_mode & 0o777,
               'cold_payload': cold, 'qualification': proof}
    if args.retry:
        saved = json.loads((BACKUP / 'receipt.json').read_text())
        if any(saved[key] != receipt[key] for key in ['uuid', 'previous_launcher_sha256', 'previous_release_sha256',
                                                     'new_launcher_sha256', 'new_release_sha256', 'launcher_mode', 'release_mode']):
            raise ValueError('Retry requires the unchanged prior and qualified runtime selection')
        if saved['cold_payload'] != cold:
            raise ValueError('Retry cold qualification differs')
        if image_uuid(DEST / 'Frameworks/SystemConfiguration.framework/Versions/A/SystemConfiguration') != uuid:
            raise ValueError('Installed qualified image metadata changed')
    print(json.dumps({'apply': args.apply, 'destination': str(DEST), 'rollback': str(BACKUP), 'uuid': uuid}))
    if not args.apply:
        return
    if os.uname().sysname != 'Darwin':
        raise ValueError('Apply requires the target')
    if not args.retry:
        BACKUP.mkdir()
        (BACKUP / 'launcher').write_bytes(prior_launcher)
        (BACKUP / 'release.json').write_bytes(prior_release)
        (BACKUP / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
        DEST.mkdir(parents=True)
        shutil.copytree(source / 'Frameworks', DEST / 'Frameworks', symlinks=True)
        # Destination is still cold here; never hash this Mach-O after it runs.
        for name, digest in expected.items():
            if name.startswith('Frameworks/') and sha((DEST / name).read_bytes()) != digest:
                raise ValueError('Cold destination differs: ' + name)
    try:
        atomic(launcher, candidate, receipt['launcher_mode'])
        atomic(release, new_release, receipt['release_mode'])
    except BaseException:
        atomic(launcher, prior_launcher, receipt['launcher_mode'])
        atomic(release, prior_release, receipt['release_mode'])
        raise

if __name__ == '__main__':
    main()
