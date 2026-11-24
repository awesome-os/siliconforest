#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Select the qualified complete audio family; plan unless --apply.

Caller holds the shared GUI/core/bench locks after notice and a fresh gate.
No provider bytes, canonical files, owner profiles, or processes are changed.
Run the installed audio acceptance packet immediately; --rollback on failure.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.machinery
import importlib.util
import json
import os
from pathlib import Path
import runpy

ROOT = Path('/usr/local/lib/siliconforest/firefox/160-system1')
APP = Path('/Applications/Firefox.app')
FAMILY = ROOT / 'overrides/audio-clock-v1'
BACKUP = ROOT / 'maintenance-audio-clock-v1'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source', type=Path, required=True)
    ap.add_argument('--apply', action='store_true')
    ap.add_argument('--rollback', action='store_true')
    args = ap.parse_args()
    source = args.source.resolve()
    if source.parent != Path('/private/var/db/sffx'):
        raise ValueError('Expected an owned private staging directory')
    launcher, release = APP / 'Contents/MacOS/firefox-sf', ROOT / 'release.json'
    installation = json.loads((ROOT / 'installation.json').read_text())
    if json.loads((APP / 'Contents/siliconforest-install.json').read_text()) != {
            'runtime': str(ROOT), 'token': installation['token']}:
        raise ValueError('Application/runtime ownership changed')
    # Share the already qualified atomic writer and metadata-only UUID reader.
    helpers = runpy.run_path(str(Path(__file__).with_name('install-network-runtime.py')))
    atomic, image_uuid = helpers['atomic'], helpers['image_uuid']
    if args.rollback:
        receipt = json.loads((BACKUP / 'receipt.json').read_text())
        if (sha(launcher.read_bytes()) != receipt['new_launcher_sha256'] or
                sha(release.read_bytes()) != receipt['new_release_sha256']):
            raise ValueError('Later runtime changes must not be overwritten')
        print(json.dumps({'action': 'restore launcher/manifest', 'apply': args.apply}))
        if args.apply:
            if os.uname().sysname != 'Darwin':
                raise ValueError('Apply requires the target')
            atomic(launcher, (BACKUP / 'launcher').read_bytes(), receipt['launcher_mode'])
            atomic(release, (BACKUP / 'release.json').read_bytes(), receipt['release_mode'])
        return
    if BACKUP.exists():
        raise ValueError('Existing backup: inspect it rather than overwriting')
    cold = json.loads((source / 'cold-verified.json').read_text())
    if cold.get('cold_before_execution') is not True or cold.get('destination') != str(source):
        raise ValueError('Matching cold text/tool staging receipt required')
    for name, digest in cold['sha256'].items():
        if sha((source / name).read_bytes()) != digest:
            raise ValueError('Staged text changed: ' + name)
    previous_launcher, previous_release = launcher.read_bytes(), release.read_bytes()
    if json.loads((source / 'baseline.json').read_text()) != {
            'launcher_sha256': sha(previous_launcher), 'release_sha256': sha(previous_release)}:
        raise ValueError('Runtime changed since preparation')
    manifest = json.loads(previous_release)
    if (manifest.get('target') != str(ROOT) or manifest.get('system_layout') is not True or
            manifest.get('system_ui') is not True or manifest.get('system_gpu_providers') is not True or
            manifest.get('network_override') != str(ROOT / 'overrides/network-v1/Frameworks') or
            manifest.get('diagnostics') or manifest.get('audio_override') is not None):
        raise ValueError('Expected the qualified system graphics/network runtime')
    ledger = json.loads((FAMILY / 'install-ledger.json').read_text())
    qualified = json.loads((source / 'audio-qualification.json').read_text())
    if (ledger.get('target') != str(FAMILY) or ledger.get('cold_before_execution') is not True or
            ledger.get('files') != qualified['cold_files']):
        raise ValueError('Original complete-family cold provenance differs')
    controls = qualified['private_state_controls']
    if len(controls) != 2 or any(c.get('output_boundary_passed') is not True or
            len(c.get('phases', [])) != 10 for c in controls):
        raise ValueError('Two complete private PCM-state qualifications required')
    expected = {'AudioToolbox': '5D7921BA-BA67-3E9E-A022-89FBFB80EC68',
                'AudioToolbox.output-sf': '228BE0CF-3352-3292-828D-FF9BC7D25888'}
    base = FAMILY / 'Frameworks/AudioToolbox.framework/Versions/A'
    # Never hash executed images or promote target readback as pristine bytes.
    if {name: image_uuid(base / name) for name in expected} != expected:
        raise ValueError('Staged complete family is not the qualified image pair')
    manifest.update(audio_override=str(FAMILY / 'Frameworks'), audio_output_clock=True)
    loader = importlib.machinery.SourceFileLoader('audio_candidate_launcher', str(source / 'firefox-sf'))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    candidate = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(candidate)
    command, env = candidate.plan(ROOT, [], manifest)
    if (env.get('SF_AU_NORMALIZE_HAL_CLOCK') != '1' or env.get('MOZ_CRASHREPORTER_DISABLE') != '1' or
            env['DYLD_FRAMEWORK_PATH'].split(':')[0] != str(FAMILY / 'Frameworks') or
            env['DYLD_LIBRARY_PATH'].split(':')[0] != '/usr/local/lib/siliconforest/opengl/v1/lib' or
            command[1] != '/usr/libexec/iosurfaced'):
        raise ValueError('Candidate launch plan does not preserve the qualified cohort')
    new_launcher = (source / 'firefox-sf').read_bytes()
    new_release = (json.dumps(manifest, indent=2) + '\n').encode()
    receipt = dict(installed_at=datetime.now(timezone.utc).isoformat(), images=expected,
        previous_launcher_sha256=sha(previous_launcher), previous_release_sha256=sha(previous_release),
        new_launcher_sha256=sha(new_launcher), new_release_sha256=sha(new_release),
        launcher_mode=launcher.stat().st_mode & 0o777, release_mode=release.stat().st_mode & 0o777,
        qualification=qualified, pending_installed_acceptance=True)
    print(json.dumps(dict(apply=args.apply, family=str(FAMILY), backup=str(BACKUP), images=expected)))
    if not args.apply:
        return
    if os.uname().sysname != 'Darwin':
        raise ValueError('Apply requires the target')
    BACKUP.mkdir()
    (BACKUP / 'launcher').write_bytes(previous_launcher)
    (BACKUP / 'release.json').write_bytes(previous_release)
    (BACKUP / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    try:
        atomic(launcher, new_launcher, receipt['launcher_mode'])
        atomic(release, new_release, receipt['release_mode'])
    except BaseException:
        atomic(launcher, previous_launcher, receipt['launcher_mode'])
        atomic(release, previous_release, receipt['release_mode'])
        raise


if __name__ == '__main__':
    main()
