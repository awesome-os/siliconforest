#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Select the qualified decoder-buffer family; dry plan unless --apply.

Caller holds GUI/core/bench after notice and a fresh coordination gate. An
installed acceptance run must follow immediately, with --rollback on failure.
Provider bytes, canonical libraries, owner profiles and processes are untouched.
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
FAMILY = ROOT / 'overrides/video-shared-v1'
BACKUP = ROOT / 'maintenance-video-shared-v1'
IMAGES = {
    'CoreVideo.framework/Versions/A/CoreVideo': 'ADF50510-1DC8-3759-ACAF-A2AA1F7D6310',
    'VideoToolbox.framework/Versions/A/VideoToolbox': '5058E4F3-F47B-3156-81F3-88C8033EB155',
    'VideoToolbox.framework/Versions/A/VideoToolbox.pd': '49F4564E-D91A-3003-B439-36D60B73BB74',
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def qualify(proof):
    """Admission of the retained matched pair, not a panel-pacing claim."""
    phases = proof['pair']['phases']
    for mode in ['copy', 'shared']:
        phase = phases[mode]
        if phase.get('browser_survived') is not True or phase.get('measurement_passed') is not True:
            raise ValueError('Both native playback arms must complete')
        clips = phase['clips']
        if len(clips) != 2:
            raise ValueError('Both 1080p60 and 4K30 required')
        for clip, frames, dimensions in zip(clips, [600, 300], [(1920, 1080), (3840, 2160)]):
            if (clip.get('expected_frames') != frames or clip.get('decoded_frames') != frames or
                    clip.get('dropped') != 0 or clip.get('ended') is not True or clip.get('play_error')):
                raise ValueError('Incomplete, failed or dropped playback')
            pixels = clip['final_pixels']
            if (pixels['videoWidth'], pixels['videoHeight']) != dimensions or not pixels['nonBlack']:
                raise ValueError('Missing decoded test picture')
            if (clip.get('actual_renderer_source_observed') is not True or clip.get('capture_error') or
                    clip['capture'].get('source') != 'renderer' or clip['capture'].get('owner_input_overlap')):
                raise ValueError('Actual renderer proof required, not a CPU scene capture')
            delta = clip['avd_delta']
            if delta['jobs'] != frames or delta['done'] != frames or any(
                    delta[key] for key in ['errors', 'timeouts', 'dart_faults', 'quarantined_desc']):
                raise ValueError('Hardware decode job/error gate failed')
            gpu = clip['gpu_delta']
            if gpu['cpu_upload_bytes'] or gpu['cpu_readback_bytes']:
                raise ValueError('CPU transport copy observed')
            if mode == 'shared' and clip['vt_counter']['copied_bytes']:
                raise ValueError('Shared decoder still copies pixels')
            if not clip['cpu_family_complete']:
                raise ValueError('Incomplete browser-family CPU accounting')
    for old, new in zip(phases['copy']['clips'], phases['shared']['clips']):
        if (old['final_pixels'] != new['final_pixels'] or
                new['matched_family_cpu'] > old['matched_family_cpu'] or
                new['cpu_percent_one_core']['rdd'] > old['cpu_percent_one_core']['rdd']):
            raise ValueError('Pixel mismatch or CPU regression in the matched pair')
    reviewed = proof['review']['captures']
    if {(r['mode'], r['clip']) for r in reviewed} != {(m, n) for m in ['copy', 'shared'] for n in [0, 1]} or any(
            r.get('looked') is not True or r.get('source') != 'renderer' for r in reviewed):
        raise ValueError('All four actual renderer pictures require review')


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
    helpers = runpy.run_path(str(Path(__file__).with_name('install-network-runtime.py')))
    atomic, image_uuid = helpers['atomic'], helpers['image_uuid']
    if args.rollback:
        receipt = json.loads((BACKUP / 'receipt.json').read_text())
        if (sha(launcher.read_bytes()) != receipt['new_launcher_sha256'] or
                sha(release.read_bytes()) != receipt['new_release_sha256']):
            raise ValueError('Later runtime changes must not be overwritten')
        print(json.dumps(dict(action='restore launcher/manifest', apply=args.apply)))
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
        raise ValueError('Matching cold text/tool receipt required')
    for name, digest in cold['sha256'].items():
        if sha((source / name).read_bytes()) != digest:
            raise ValueError('Staged text changed: ' + name)
    previous_launcher, previous_release = launcher.read_bytes(), release.read_bytes()
    if json.loads((source / 'baseline.json').read_text()) != {
            'launcher_sha256': sha(previous_launcher), 'release_sha256': sha(previous_release)}:
        raise ValueError('Runtime changed since preparation')
    manifest = json.loads(previous_release)
    expected = dict(target=str(ROOT), system_layout=True, system_ui=True, system_gpu_providers=True,
        network_override=str(ROOT / 'overrides/network-v1/Frameworks'),
        audio_override=str(ROOT / 'overrides/audio-clock-v1/Frameworks'), audio_output_clock=True)
    if any(type(manifest.get(k)) is not type(v) or manifest.get(k) != v for k, v in expected.items()) or any(
            manifest.get(k) for k in ['diagnostics', 'graphics_override', 'video_override', 'video_zero_copy']):
        raise ValueError('Expected qualified system graphics/network/audio runtime')
    proof = json.loads((source / 'video-qualification.json').read_text())
    qualify(proof)
    ledger = json.loads((FAMILY / 'install-ledger.json').read_text())
    if (ledger.get('target') != str(FAMILY) or ledger.get('cold_before_execution') is not True or
            ledger.get('files') != proof['cold_files']):
        raise ValueError('Original complete-family cold provenance differs')
    names = {name.split('/')[1] for name in proof['cold_files'] if name.startswith('Frameworks/')}
    if names != {'CoreVideo.framework', 'VideoToolbox.framework'}:
        raise ValueError('Only the complete qualified CoreVideo/VideoToolbox family allowed')
    # Executed Mach-O pages are PAC-lowered; inspect metadata, never hot hashes.
    if {name: image_uuid(FAMILY / 'Frameworks' / name) for name in IMAGES} != IMAGES:
        raise ValueError('Selected images differ from the native-qualified family')
    manifest.update(video_override=str(FAMILY / 'Frameworks'), video_zero_copy=True)
    loader = importlib.machinery.SourceFileLoader('video_candidate_launcher', str(source / 'firefox-sf'))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    candidate = importlib.util.module_from_spec(spec);loader.exec_module(candidate)
    command, env = candidate.plan(ROOT, [], manifest)
    if (env.get('SF_VT_ZERO_COPY') != '1' or env.get('SF_VT_RETIRE_POOL_AT_INVALIDATE') == '1' or
            env.get('SF_AU_NORMALIZE_HAL_CLOCK') != '1' or env.get('MOZ_CRASHREPORTER_DISABLE') != '1' or
            env['DYLD_FRAMEWORK_PATH'].split(':')[:3] != [manifest[k] for k in ['video_override', 'audio_override', 'network_override']] or
            env['DYLD_LIBRARY_PATH'].split(':')[0] != '/usr/local/lib/siliconforest/opengl/v1/lib' or
            command[1] != '/usr/libexec/iosurfaced'):
        raise ValueError('Candidate plan does not preserve the qualified cohort')
    new_launcher = (source / 'firefox-sf').read_bytes()
    new_release = (json.dumps(manifest, indent=2) + '\n').encode()
    receipt = dict(installed_at=datetime.now(timezone.utc).isoformat(), images=IMAGES,
        previous_launcher_sha256=sha(previous_launcher), previous_release_sha256=sha(previous_release),
        new_launcher_sha256=sha(new_launcher), new_release_sha256=sha(new_release),
        launcher_mode=launcher.stat().st_mode & 0o777, release_mode=release.stat().st_mode & 0o777,
        qualification=proof, pending_installed_acceptance=True)
    print(json.dumps(dict(apply=args.apply, family=str(FAMILY), backup=str(BACKUP), images=IMAGES)))
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
