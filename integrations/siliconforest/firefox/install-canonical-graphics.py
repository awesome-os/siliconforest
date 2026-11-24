#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Promote our qualified CGL/GL dispatch and select all system graphics.

Plan by default. Apply under gui.lock after a disruption notice and fresh gate.
Only our two graphics images, text launcher and Firefox manifest change; preserve
both former images, the launcher and manifest. No owner profile, input, process or core ABI is changed.
"""
import argparse
from datetime import datetime, timezone
import fcntl
import hashlib
import importlib.machinery
import importlib.util
import json
import os
from pathlib import Path
import shutil

ROOT = Path('/usr/local/lib/siliconforest/firefox/160-system1')
DESTINATIONS = {
    'libGL.1.dylib': Path('/usr/local/lib/siliconforest/opengl/v1/lib/libGL.1.dylib'),
    'OpenGL': Path('/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL'),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--rollback', action='store_true',
                        help='plan restoration of this promotion; --apply restores it')
    args = parser.parse_args()
    source = args.source.resolve()
    if not source.is_relative_to('/private/var/db/sffx'):
        raise ValueError('Expected a new private cold install source')
    cold = json.loads((source/'cold-verified.json').read_text())
    names = ['install-system-ui.py', 'install-canonical-graphics.py', 'firefox-sf', 'OpenGL', 'libGL.1.dylib',
             'native-results.json', 'nv12.log', 'bgra.log', 'browser-result.json',
             'browser-support.json', 'browser-control.json', 'baseline-identities.json']
    if cold.get('destination') != str(source) or cold.get('cold_before_execution') is not True:
        raise ValueError('Matching cold receipt required')
    for name in names:
        if hashlib.sha256((source/name).read_bytes()).hexdigest() != cold['sha256'].get(name):
            raise ValueError('Unexecuted cold source changed: '+name)
    spec = importlib.util.spec_from_file_location('canonical_helpers', source/'install-system-ui.py')
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    native = json.loads((source/'native-results.json').read_text())
    for case in ['system-surface-nv12', 'system-surface-bgra']:
        if not any(row.get('case')==case and row.get('returncode')==0 for row in native):
            raise ValueError('Canonical surface native control failed: '+case)
    if 'malformed_layouts_rejected=4 cases=12 pass=1' not in (source/'nv12.log').read_text():
        raise ValueError('Twelve completed NV12 lease/pixel cases required')
    if 'cpu_upload=0 cpu_readback=0' not in (source/'bgra.log').read_text():
        raise ValueError('Native zero-CPU BGRA transport evidence required')
    browser = json.loads((source/'browser-result.json').read_text())
    control = json.loads((source/'browser-control.json').read_text())
    scene = browser.get('scene_reference', {})
    graphics = json.loads((source/'browser-support.json').read_text()).get('graphics', {})
    if not (browser.get('measurement_passed') is True and browser.get('browser_survived_run') is True
            and scene.get('complete') is True and scene.get('hash')=='e4bc0ce1'
            and scene.get('glError')==0 and scene.get('coordinate_calibration',{}).get('complete') is True
            and control.get('provider_control')=='system-surface-candidate-gl'
            and graphics.get('windowLayerManagerType')=='WebRender'
            and graphics.get('numAcceleratedWindows',0)>0):
        raise ValueError('Matching hardware browser renderer gate did not pass')
    uuids = {name: helper.image_uuid(source/name) for name in DESTINATIONS}
    providers = browser.get('providers', {})
    if providers.get('complete') is not True or providers.get('private_windowserver_expected'):
        raise ValueError('Canonical Cocoa provider observation required')
    rows = providers.get('processes', [])
    if {r.get('role') for r in rows} != {'parent','gpu'}:
        raise ValueError('Both real browser roles required')
    for row in rows:
        if row.get('passed') is not True:
            raise ValueError('Provider gate failed')
        for name in DESTINATIONS:
            if not any('<'+uuids[name]+'>' in line and line.endswith('/'+name) for line in row['images']):
                raise ValueError('Candidate image UUID not observed in browser: '+name)
        surface = '/System/Library/Frameworks/IOSurface.framework/Versions/A/IOSurface'
        if not any(line.endswith('> '+surface) and '<'+helper.image_uuid(Path(surface))+'>' in line
                   for line in row['images']):
            raise ValueError('Qualified system IOSurface changed or was not used')
    previous = (ROOT/'release.json').read_bytes()
    launcher = Path('/Applications/Firefox.app/Contents/MacOS/firefox-sf')
    previous_launcher = launcher.read_bytes()
    manifest = json.loads(previous)
    install = json.loads((ROOT/'installation.json').read_text())
    marker = json.loads(Path('/Applications/Firefox.app/Contents/siliconforest-install.json').read_text())
    if marker != dict(runtime=str(ROOT),token=install['token']):
        raise ValueError('Installed application ownership marker differs')
    if (manifest.get('target') != str(ROOT) or manifest.get('system_ui') is not True
            or manifest.get('system_layout') is not True or manifest.get('diagnostics')):
        raise ValueError('Canonical UI cohort must remain selected')
    baseline = json.loads((source/'baseline-identities.json').read_text())
    backup = ROOT/'maintenance-canonical-graphics-v3'
    if args.rollback:
        receipt = json.loads((backup/'receipt.json').read_text())
        if (receipt.get('candidate_uuids') != uuids or
                receipt.get('installed_manifest_sha256') != hashlib.sha256(previous).hexdigest() or
                receipt.get('installed_launcher_sha256') != hashlib.sha256(previous_launcher).hexdigest()):
            raise ValueError('Installed selection changed since promotion; review before rollback')
        original = (backup/'release.json').read_bytes()
        if hashlib.sha256(original).hexdigest() != receipt['previous_manifest_sha256']:
            raise ValueError('Original selector changed')
        original_launcher = (backup/'firefox-sf').read_bytes()
        if hashlib.sha256(original_launcher).hexdigest() != receipt['previous_launcher_sha256']:
            raise ValueError('Original launcher changed')
        for name,path in DESTINATIONS.items():
            if (path.is_symlink() or helper.image_uuid(path) != uuids[name] or
                    helper.image_uuid(backup/name) != baseline[name]):
                raise ValueError('Provider changed since promotion: '+name)
        print(json.dumps(dict(rollback=True,backup=str(backup),
                              restored_uuids=baseline,process_signals=False),indent=2),flush=True)
        if not args.apply:
            return
        if os.uname().sysname != 'Darwin':
            raise RuntimeError('Apply only on SiliconForest')
        with (ROOT/'.graphics-maintenance.lock').open('a') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX)
            if (ROOT/'release.json').read_bytes()!=previous:
                raise RuntimeError('Runtime selection changed while planning rollback')
            if launcher.read_bytes()!=previous_launcher:
                raise RuntimeError('Launcher changed while planning rollback')
            # Select the still-present private cohort before changing providers.
            helper.atomic(ROOT/'release.json',original,0o600)
            for name,path in reversed(list(DESTINATIONS.items())):
                helper.atomic(path,(backup/name).read_bytes(),(backup/name).stat().st_mode & 0o777)
            helper.atomic(launcher,original_launcher,(backup/'firefox-sf').stat().st_mode & 0o777)
            (backup/'rolled-back.json').write_text(json.dumps(dict(
                at=datetime.now(timezone.utc).isoformat(),restored_uuids=baseline),indent=2)+'\n')
        return
    for name, path in DESTINATIONS.items():
        if not path.is_file() or path.is_symlink() or helper.image_uuid(path) != baseline.get(name):
            raise ValueError('Existing canonical provider changed: '+name)
    manifest.update(system_gpu_providers=True)
    manifest.pop('graphics_override', None)
    manifest.pop('surface_override', None)
    loader = importlib.machinery.SourceFileLoader('canonical_launcher',str(source/'firefox-sf'))
    candidate_spec = importlib.util.spec_from_loader('canonical_launcher',loader)
    candidate = importlib.util.module_from_spec(candidate_spec)
    loader.exec_module(candidate)
    command,environment = candidate.plan(ROOT,[],manifest=manifest)
    if (command[1]!='/usr/libexec/iosurfaced' or
            environment['DYLD_LIBRARY_PATH'].split(':')[0]!=
            '/usr/local/lib/siliconforest/opengl/v1/lib'):
        raise ValueError('Canonical broker and first-priority matched GL dispatch required')
    if backup.exists():
        raise FileExistsError('Rollback already exists; inspect it before another promotion')
    plan = dict(destinations={k:str(v) for k,v in DESTINATIONS.items()},candidate_uuids=uuids,
                previous_uuids=baseline,backup=str(backup),system_gpu_providers=True,
                library_path=environment['DYLD_LIBRARY_PATH'],launcher=str(launcher),
                profiles_changed=False,process_signals=False,installed_outer_verification_required=True)
    print(json.dumps(plan,indent=2),flush=True)
    if not args.apply:
        return
    if os.uname().sysname != 'Darwin':
        raise RuntimeError('Apply only on SiliconForest')
    with (ROOT/'.graphics-maintenance.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        if (ROOT/'release.json').read_bytes()!=previous:
            raise RuntimeError('Runtime selection changed while planning')
        if launcher.read_bytes()!=previous_launcher:
            raise RuntimeError('Launcher changed while planning')
        for name,path in DESTINATIONS.items():
            if path.is_symlink() or helper.image_uuid(path) != baseline[name]:
                raise RuntimeError('Canonical provider changed while planning: '+name)
        backup.mkdir(mode=0o700)
        (backup/'release.json').write_bytes(previous)
        shutil.copy2(launcher,backup/'firefox-sf')
        for name,path in DESTINATIONS.items():
            # Existing images may already be PAC-lowered: preserve their bytes
            # for rollback, never describe these copies as cold qualification.
            shutil.copy2(path,backup/name)
        try:
            # Old CGL with expanded dispatch already paints; install API first.
            for name,path in DESTINATIONS.items():
                helper.atomic(path,(source/name).read_bytes(),path.stat().st_mode & 0o777)
            helper.atomic(launcher,(source/'firefox-sf').read_bytes(),launcher.stat().st_mode & 0o777)
            helper.atomic(ROOT/'release.json',(json.dumps(manifest,indent=2)+'\n').encode(),0o600)
            plan['installed_at']=datetime.now(timezone.utc).isoformat()
            plan['previous_manifest_sha256']=hashlib.sha256(previous).hexdigest()
            plan['installed_manifest_sha256']=hashlib.sha256((ROOT/'release.json').read_bytes()).hexdigest()
            plan['previous_launcher_sha256']=hashlib.sha256(previous_launcher).hexdigest()
            plan['installed_launcher_sha256']=hashlib.sha256(launcher.read_bytes()).hexdigest()
            (backup/'receipt.json').write_text(json.dumps(plan,indent=2)+'\n')
        except BaseException:
            helper.atomic(ROOT/'release.json',previous,0o600)
            helper.atomic(launcher,previous_launcher,(backup/'firefox-sf').stat().st_mode & 0o777)
            for name,path in reversed(list(DESTINATIONS.items())):
                helper.atomic(path,(backup/name).read_bytes(),(backup/name).stat().st_mode & 0o777)
            raise
    print('Canonical graphics promoted; installed outer renderer gate remains required')


if __name__ == '__main__':
    main()
