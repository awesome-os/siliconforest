#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Select the versioned NV12 cohort after matching native and browser gates.

Default prints a plan. Only --apply changes our launcher/manifest text, retaining
both previous files. Provider files must already be installed at a NEW path.
No running process, owner profile, shared framework, desktop or input is touched.
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


def load(name, path):
    loader = importlib.machinery.SourceFileLoader(name, str(path))
    spec = importlib.util.spec_from_loader(name, loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source', type=Path, required=True, help='cold-verified launcher and gate evidence')
    ap.add_argument('--apply', action='store_true')
    args = ap.parse_args()
    source = args.source.resolve()
    root = Path('/usr/local/lib/siliconforest/firefox/160-system1')
    maintenance_lock = None
    if args.apply:
        if os.uname().sysname != 'Darwin':
            raise RuntimeError('Apply only on SiliconForest')
        maintenance_lock = (root / '.graphics-maintenance.lock').open('a')
        fcntl.flock(maintenance_lock, fcntl.LOCK_EX)
    cohort = root / 'overrides/nv12-v1'
    launcher = Path('/Applications/Firefox.app/Contents/MacOS/firefox-sf')
    manifest_path = root / 'release.json'
    backup = root / 'maintenance-nv12-v1'
    helper = load('graphics_install_helpers', Path(__file__).with_name('install-system-ui.py'))
    cold = json.loads((source / 'cold-verified.json').read_text())
    if cold.get('destination') != str(source) or cold.get('cold_before_execution') is not True:
        raise ValueError('Matching cold source receipt required')
    names = ['firefox-sf', 'native-results.json', 'nv12-lease.log', 'bgra-cross.log',
             'browser-result.json', 'browser-control.json', 'browser-support.json', 'identities.json']
    for name in names:
        if helper.sha(source / name) != cold['sha256'].get(name):
            raise ValueError('Cold source changed: ' + name)
    install = json.loads((root / 'installation.json').read_text())
    if json.loads(launcher.parent.parent.joinpath('siliconforest-install.json').read_text()) != dict(
            runtime=str(root), token=install['token']):
        raise ValueError('App/runtime ownership markers disagree')
    previous = manifest_path.read_bytes()
    manifest = json.loads(previous)
    if (manifest.get('target') != str(root) or manifest.get('system_layout') is not True or
            manifest.get('system_ui') is not True or manifest.get('diagnostics')):
        raise ValueError('Expected the installed system UI cohort without active diagnostics')
    native = json.loads((source / 'native-results.json').read_text())
    if ([(r.get('name'), r.get('returncode')) for r in native] !=
            [('nv12-lease', 0), ('bgra-cross', 0)] or
            'malformed_layouts_rejected=4 cases=12 pass=1' not in (source / 'nv12-lease.log').read_text() or
            'FXCGLSURFACE failures=0' not in (source / 'bgra-cross.log').read_text()):
        raise ValueError('Native completion-reuse and BGRA gates did not pass')
    browser = json.loads((source / 'browser-result.json').read_text())
    control = json.loads((source / 'browser-control.json').read_text())
    scene = browser.get('scene_reference', {})
    graphics = json.loads((source / 'browser-support.json').read_text()).get('graphics', {})
    if (control.get('provider_control') != 'nv12-cohort' or control.get('installed_outer_or_dock') is not False or
            browser.get('measurement_passed') is not True or browser.get('browser_survived_run') is not True or
            scene.get('complete') is not True or scene.get('hash') != 'e4bc0ce1' or scene.get('glError') != 0 or
            scene.get('coordinate_calibration', {}).get('complete') is not True):
        raise ValueError('Matching private browser renderer gate did not pass')
    if (graphics.get('windowLayerManagerType') != 'WebRender' or
            graphics.get('numAcceleratedWindows', 0) < 1 or
            'Apple M1' not in graphics.get('webgl1Renderer', '')):
        raise ValueError('Actual hardware WebRender and M1 WebGL observations required')
    expected = dict(IOSurface=cohort / 'surface/Frameworks/IOSurface.framework/Versions/A/IOSurface',
                    OpenGL=cohort / 'gl/Frameworks/OpenGL.framework/Versions/A/OpenGL',
                    iosurfaced=cohort / 'surface/bin/iosurfaced')
    identities = json.loads((source / 'identities.json').read_text())
    if set(identities) != set(expected):
        raise ValueError('Both graphics providers and their broker identities are required')
    for name, path in expected.items():
        if helper.image_uuid(path) != identities[name]['uuid']:
            raise ValueError('Prepared graphics provider changed: ' + name)
    providers = browser.get('providers', {})
    rows = providers.get('processes', [])
    if (providers.get('complete') is not True or providers.get('private_windowserver_expected') or
            providers.get('canonical_cocoa_complete') is False or
            {r.get('role') for r in rows} != {'parent', 'gpu'}):
        raise ValueError('Actual parent and GPU provider observations required')
    for row in rows:
        if row.get('passed') is not True:
            raise ValueError('System UI provider gate failed')
        for name in ['IOSurface', 'OpenGL']:
            if not any(line.endswith('> ' + str(expected[name])) and
                       '<' + identities[name]['uuid'] + '>' in line for line in row['images']):
                raise ValueError('Browser did not load the prepared cohort: ' + name)
    manifest.update(graphics_override=str(cohort), system_gpu_providers=False)
    manifest.pop('surface_override', None)
    candidate = load('graphics_candidate_launcher', source / 'firefox-sf')
    command, environment = candidate.plan(root, [], manifest=manifest)
    if command[1] != str(expected['iosurfaced']):
        raise ValueError('Candidate selected a different broker')
    if backup.exists():
        raise FileExistsError('Activation backup already exists; inspect its receipt')
    plan = dict(cohort=str(cohort), command=command,
                framework_path=environment['DYLD_FRAMEWORK_PATH'], library_path=environment['DYLD_LIBRARY_PATH'],
                native_controls_passed=True, private_browser_scene_passed=True,
                installed_outer_after_activation_verified=False, backup=str(backup),
                owner_profiles_changed=False, process_signals=False)
    print(json.dumps(plan, indent=2), flush=True)
    if not args.apply:
        return
    if os.uname().sysname != 'Darwin':
        raise RuntimeError('Apply only on SiliconForest')
    backup.mkdir(mode=0o700)
    (backup / 'release.json').write_bytes(previous)
    (backup / 'firefox-sf').write_bytes(launcher.read_bytes())
    (backup / 'firefox-sf').chmod(0o755)
    try:
        helper.atomic(launcher, (source / 'firefox-sf').read_bytes(), 0o755)
        helper.atomic(manifest_path, (json.dumps(manifest, indent=2) + '\n').encode(), 0o600)
        plan.update(installed_at=datetime.now(timezone.utc).isoformat(),
                    previous_manifest_sha256=hashlib.sha256(previous).hexdigest())
        (backup / 'receipt.json').write_text(json.dumps(plan, indent=2) + '\n')
    except BaseException:
        helper.atomic(manifest_path, previous, 0o600)
        helper.atomic(launcher, (backup / 'firefox-sf').read_bytes(), 0o755)
        raise
    print('Versioned graphics cohort selected; installed outer verification remains required')


if __name__ == '__main__':
    main()
