#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Plan/apply a qualified, versioned Firefox-only Onyx2D update.

Run under gui.lock after coordination admission. The staging tree must be a
new cold copy from the Linux build, never a copy of an executed target image.
An existing Firefox process is never signalled or updated in place.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    source = args.source.resolve()
    root = Path('/usr/local/lib/siliconforest/firefox/160-system1')
    app = Path('/Applications/Firefox.app')
    manifest_path = root / 'release.json'
    launcher = app / 'Contents/MacOS/firefox-sf'
    previous = manifest_path.read_bytes()
    manifest = json.loads(previous)
    installation = json.loads((root / 'installation.json').read_text())
    if json.loads((app / 'Contents/siliconforest-install.json').read_text()) != dict(
            runtime=str(root), token=installation['token']):
        raise ValueError('App and runtime ownership markers disagree')
    if manifest['target'] != str(root) or not manifest['system_layout'] or manifest.get('diagnostics'):
        raise ValueError('Unexpected runtime or active installation diagnostics')
    if manifest.get('onyx_override'):
        raise RuntimeError('An Onyx override is already selected; review before replacing it')
    qualification = json.loads((source / 'qualification.json').read_text())
    required = ['actual_renderer_reviewed', 'web_fonts_enabled', 'hardware_webrender',
                'provider_uuid_matched', 'mozilla_unmodified', 'glyph_identity_reviewed']
    if not all(qualification.get(key) is True for key in required):
        raise ValueError('Native font identity, renderer and provider qualification is incomplete')
    if not {'cnn.com', 'nytimes.com'} <= set(qualification.get('sites', [])):
        raise ValueError('Both CNN and NYT glyph checks are required')
    destination = root / 'overrides/glyph-generation-v1'
    backup = root / 'maintenance-glyph-generation-v1'
    if destination.exists() or backup.exists():
        raise FileExistsError('Update and backup must have new names')
    spec = importlib.util.spec_from_file_location('font_installer',
        Path(__file__).with_name('install-font-provider.py'))
    helpers = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helpers)
    cold = json.loads((source / 'cold-verified.json').read_text())
    if cold.get('cold_before_execution') is not True or cold.get('destination') != str(source):
        raise ValueError('Matching cold staging receipt is required')
    for name, wanted in cold['sha256'].items():
        if helpers.sha(source / name) != wanted:
            raise ValueError('Cold staging changed: ' + name)
    for name, wanted in cold['symlinks'].items():
        if str((source / name).readlink()) != wanted:
            raise ValueError('Cold staging link changed: ' + name)
    image = 'provider/Frameworks/Onyx2D.framework/Versions/A/Onyx2D'
    if cold['sha256'].get(image) != qualification.get('onyx_sha256'):
        raise ValueError('Qualified and staged Onyx2D differ')
    manifest['onyx_override'] = str(destination / 'Frameworks')
    # Validate the precise new launcher and manifest before replacing either.
    # The installed file has no .py suffix, so explicitly use a source loader.
    from importlib.machinery import SourceFileLoader
    spec = importlib.util.spec_from_loader('new_launcher', SourceFileLoader('new_launcher', str(source / 'firefox-sf')))
    candidate = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(candidate)
    _, environment = candidate.plan(root, [], manifest=manifest)
    if str(destination / 'Frameworks') not in environment['DYLD_FRAMEWORK_PATH'].split(':'):
        raise ValueError('Candidate launcher does not select the staged glyph provider')
    plan = dict(runtime=str(root), new_provider=str(destination), backup=str(backup),
                qualified=qualification, canonical_frameworks_replaced=False, owner_profiles_changed=False)
    print(json.dumps(plan, indent=2), flush=True)
    if not args.apply:
        return
    if os.uname().sysname != 'Darwin':
        raise RuntimeError('Target must be SiliconForest')
    running = subprocess.run(['ps', '-axo', 'command='], capture_output=True, text=True, check=True).stdout
    if any(str(app / 'Contents/Resources/Mozilla.app/Contents/MacOS/firefox') in line
           for line in running.splitlines()):
        raise RuntimeError('Installed Firefox is running; no process signalled')
    backup.mkdir(mode=0o700)
    (backup / 'release.json').write_bytes(previous)
    (backup / 'firefox-sf').write_bytes(launcher.read_bytes())
    (backup / 'firefox-sf').chmod(0o755)
    destination.parent.mkdir(exist_ok=True)
    shutil.copytree(source / 'provider', destination, symlinks=True)
    verified = {}
    for path in destination.rglob('*'):
        name = 'provider/' + str(path.relative_to(destination))
        if path.is_symlink():
            if str(path.readlink()) != cold['symlinks'][name]:
                raise ValueError('Final provider link mismatch: ' + name)
        elif path.is_file():
            verified[name] = helpers.sha(path)
            if verified[name] != cold['sha256'][name]:
                raise ValueError('Final cold provider mismatch: ' + name)
    try:
        helpers.atomic(launcher, (source / 'firefox-sf').read_bytes(), 0o755)
        helpers.atomic(manifest_path, (json.dumps(manifest, indent=2) + '\n').encode())
        receipt = dict(plan, installed_at=datetime.now(timezone.utc).isoformat(),
            final_cold_provider_files=verified, launcher_sha256=helpers.sha(launcher),
            previous_manifest_sha256=hashlib.sha256(previous).hexdigest(),
            rollback='With Firefox closed, restore release.json and firefox-sf from this backup.')
        (backup / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    except BaseException:
        helpers.atomic(launcher, (backup / 'firefox-sf').read_bytes(), 0o755)
        helpers.atomic(manifest_path, previous)
        raise
    print(json.dumps(dict(installed=True, receipt=str(backup / 'receipt.json'))))


if __name__ == '__main__':
    main()
