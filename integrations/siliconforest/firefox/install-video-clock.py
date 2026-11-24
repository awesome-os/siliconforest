#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Select the qualified 0172 video clock; manifest-only, dry plan by default.

Use GUI/core/bench locks after notice. Immediately gate the actual installed
outer launcher, rolling back on failure. Decoder, audio, graphics and codec
policies remain selected. Original family is retained as the clock rollback.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import runpy
import shutil

ROOT = Path('/usr/local/lib/siliconforest/firefox/160-system1')
OLD = ROOT / 'overrides/video-shared-v1/Frameworks'
FAMILY = ROOT / 'overrides/video-clock-0172-v1'
BACKUP = ROOT / 'maintenance-video-clock-0172-v1'
IMAGES = {
    'CoreVideo.framework/Versions/A/CoreVideo': '9FB0E7EB-4CFA-35AB-B18F-2C9F21B5AFDE',
    'VideoToolbox.framework/Versions/A/VideoToolbox': '5058E4F3-F47B-3156-81F3-88C8033EB155',
    'VideoToolbox.framework/Versions/A/VideoToolbox.pd': '49F4564E-D91A-3003-B439-36D60B73BB74',
}


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
        raise ValueError('Expected a new owned private cold source')
    helpers = runpy.run_path(str(source / 'install-network-runtime.py'))
    atomic, image_uuid = helpers['atomic'], helpers['image_uuid']
    release = ROOT / 'release.json'
    previous = release.read_bytes()
    install = json.loads((ROOT / 'installation.json').read_text())
    marker = json.loads(Path('/Applications/Firefox.app/Contents/siliconforest-install.json').read_text())
    if marker != dict(runtime=str(ROOT), token=install['token']):
        raise ValueError('Application ownership changed')
    if args.rollback:
        receipt = json.loads((BACKUP / 'receipt.json').read_text())
        if sha(previous) != receipt['new_release_sha256']:
            raise ValueError('A later runtime change must not be overwritten')
        original = (BACKUP / 'release.json').read_bytes()
        if sha(original) != receipt['previous_release_sha256']:
            raise ValueError('Rollback bytes changed')
        print(json.dumps(dict(rollback=True, apply=args.apply)))
        if args.apply:
            if os.uname().sysname != 'Darwin':
                raise ValueError('Apply requires SiliconForest')
            atomic(release, original, receipt['release_mode'])
        return
    cold = json.loads((source / 'cold-verified.json').read_text())
    if cold.get('cold_before_execution') is not True or cold.get('destination') != str(source):
        raise ValueError('Matching cold receipt required')
    for name, digest in cold['sha256'].items():
        if sha((source / name).read_bytes()) != digest:
            raise ValueError('Cold source changed: ' + name)
    if json.loads((source / 'baseline.json').read_text())['release_sha256'] != sha(previous):
        raise ValueError('Runtime changed since preparation')
    proof = json.loads((source / 'clock-qualification.json').read_text())
    if (proof.get('private_gate_passed') is not True or not proof.get('checks') or
            not all(value is True for value in proof['checks'].values()) or
            proof.get('image_uuids') != IMAGES or proof.get('root_looked') is not True or
            proof.get('decoded_frames') != [600, 300] or proof.get('dropped_frames') != [0, 0] or
            proof.get('natural_exit') != 0 or proof.get('surviving_family') != []):
        raise ValueError('Private clock/strict playback/providers/pictures/natural-exit gate required')
    manifest = json.loads(previous)
    expected = dict(target=str(ROOT), system_layout=True, system_ui=True,
                    system_gpu_providers=True, video_override=str(OLD), video_zero_copy=True,
                    audio_output_clock=True, av1_enabled=False)
    if any(type(manifest.get(k)) is not type(v) or manifest.get(k) != v for k, v in expected.items()) or any(
            manifest.get(k) for k in ['diagnostics', 'graphics_override', 'gpu_damage', 'gpu_image_cache']):
        raise ValueError('Expected qualified runtime with damage/image cache OFF')
    if image_uuid(OLD / 'CoreVideo.framework/Versions/A/CoreVideo') != 'ADF50510-1DC8-3759-ACAF-A2AA1F7D6310':
        raise ValueError('Previous video clock changed')
    for name, expected_uuid in IMAGES.items():
        if image_uuid(source / 'Frameworks' / name) != expected_uuid:
            raise ValueError('Candidate image differs: ' + name)
        if name.startswith('VideoToolbox') and image_uuid(OLD / name) != expected_uuid:
            raise ValueError('Decoder must remain unchanged')
    if BACKUP.exists() or FAMILY.exists():
        raise ValueError('Retain an existing attempt; do not overwrite it')
    manifest['video_override'] = str(FAMILY / 'Frameworks')
    new_release = (json.dumps(manifest, indent=2) + '\n').encode()
    receipt = dict(installed_at=datetime.now(timezone.utc).isoformat(), images=IMAGES,
                   previous_release_sha256=sha(previous), new_release_sha256=sha(new_release),
                   release_mode=release.stat().st_mode & 0o777, qualification=proof,
                   pending_installed_acceptance=True)
    print(json.dumps(dict(apply=args.apply, family=str(FAMILY), backup=str(BACKUP),
                          launcher_changed=False, process_signals=False, image_cache=False, damage=False)))
    if not args.apply:
        return
    if os.uname().sysname != 'Darwin':
        raise ValueError('Apply requires SiliconForest')
    BACKUP.mkdir()
    (BACKUP / 'release.json').write_bytes(previous)
    (BACKUP / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    shutil.copytree(source / 'Frameworks', FAMILY / 'Frameworks', symlinks=True)
    # Check the new cold copy before selecting or executing it.
    for name, digest in cold['sha256'].items():
        if name.startswith('Frameworks/') and sha((FAMILY / name).read_bytes()) != digest:
            raise ValueError('Promoted cold copy differs: ' + name)
    (FAMILY / 'install-ledger.json').write_text(json.dumps(cold, indent=2) + '\n')
    if release.read_bytes() != previous:
        raise ValueError('Concurrent runtime selection changed')
    atomic(release, new_release, receipt['release_mode'])


if __name__ == '__main__':
    main()
