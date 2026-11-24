#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Select the measured hardware-codec policy after notice and GUI/core/bench locks.

Only launcher text and its owned manifest change. Immediate installed acceptance
is required. Rollback removes the policy while retaining its compatible launcher
support so a subsequent launch removes any managed preference block.
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
BACKUP = ROOT / 'maintenance-codec-policy-v1'


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
    atomic = runpy.run_path(str(source / 'install-network-runtime.py'))['atomic']
    if args.rollback:
        receipt = json.loads((BACKUP / 'receipt.json').read_text())
        if (sha(launcher.read_bytes()) != receipt['launcher_sha256'] or
                sha(release.read_bytes()) != receipt['release_sha256']):
            raise ValueError('Later runtime changes must not be overwritten')
        print(json.dumps(dict(action='remove codec policy; retain compatible launcher', apply=args.apply)))
        if args.apply:
            if os.uname().sysname != 'Darwin':
                raise ValueError('Apply requires the target')
            atomic(release, (BACKUP / 'release.json').read_bytes(), receipt['release_mode'])
        return
    if BACKUP.exists():
        raise ValueError('Existing backup: inspect rather than overwrite')
    cold = json.loads((source / 'cold-verified.json').read_text())
    if cold.get('cold_before_execution') is not True or cold.get('destination') != str(source):
        raise ValueError('Matching cold receipt required')
    for name, digest in cold['sha256'].items():
        if sha((source / name).read_bytes()) != digest:
            raise ValueError('Staged text changed: ' + name)
    old_launcher, old_release = launcher.read_bytes(), release.read_bytes()
    if json.loads((source / 'baseline.json').read_text()) != {
            'launcher_sha256': sha(old_launcher), 'release_sha256': sha(old_release)}:
        raise ValueError('Runtime changed since preparation')
    pair = json.loads((source / 'codec-pair-admission.json').read_text())
    review = json.loads((source / 'root-visual-review.json').read_text())
    if (pair.get('passed') is not True or not pair.get('checks') or
            not all(v is True for v in pair['checks'].values()) or
            review.get('looked') is not True or review.get('source') != 'renderer' or
            set(review.get('cases', [])) != {'youtube1080', 'youtubehw'}):
        raise ValueError('Matched native codec and actual-picture qualification required')
    manifest = json.loads(old_release)
    if (any(manifest.get(k) is not True for k in ['system_layout', 'system_ui',
            'system_gpu_providers', 'audio_output_clock', 'video_zero_copy']) or
            manifest.get('av1_enabled') is not None or manifest.get('diagnostics') or
            manifest.get('graphics_override') or manifest.get('target') != str(ROOT)):
        raise ValueError('Expected qualified system/media cohort without codec policy')
    manifest['av1_enabled'] = False
    loader = importlib.machinery.SourceFileLoader('codec_launcher', str(source / 'firefox-sf'))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    candidate = importlib.util.module_from_spec(spec)
    loader.exec_module(candidate)
    _, env = candidate.plan(ROOT, [], manifest)
    if (candidate.av1_preference(manifest, {}) is not False or
            candidate.av1_preference(manifest, {'SFFX_AV1': '1'}) is not True or
            env.get('MOZ_CRASHREPORTER_DISABLE') != '1' or
            env.get('SF_AU_NORMALIZE_HAL_CLOCK') != '1' or env.get('SF_VT_ZERO_COPY') != '1'):
        raise ValueError('Candidate lost the qualified launcher policy')
    new_launcher = (source / 'firefox-sf').read_bytes()
    new_release = (json.dumps(manifest, indent=2) + '\n').encode()
    receipt = dict(installed_at=datetime.now(timezone.utc).isoformat(),
        launcher_sha256=sha(new_launcher), release_sha256=sha(new_release),
        launcher_mode=launcher.stat().st_mode & 0o777, release_mode=release.stat().st_mode & 0o777,
        pair=pair, review=review, pending_installed_acceptance=True)
    print(json.dumps(dict(apply=args.apply, av1_enabled=False, override='SFFX_AV1=1', backup=str(BACKUP))))
    if not args.apply:
        return
    if os.uname().sysname != 'Darwin':
        raise ValueError('Apply requires the target')
    BACKUP.mkdir()
    (BACKUP / 'launcher').write_bytes(old_launcher)
    (BACKUP / 'release.json').write_bytes(old_release)
    (BACKUP / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    try:
        atomic(launcher, new_launcher, receipt['launcher_mode'])
        atomic(release, new_release, receipt['release_mode'])
    except BaseException:
        atomic(launcher, old_launcher, receipt['launcher_mode'])
        atomic(release, old_release, receipt['release_mode'])
        raise


if __name__ == '__main__':
    main()
