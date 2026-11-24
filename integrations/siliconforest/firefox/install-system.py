#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Install a pristine Mozilla payload and app-specific system runtime.

Run inside the coordination GUI lock after a cold-verified host transfer.
No canonical Cocoa/media/core/compositor file is replaced. Default is a plan.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import uuid

from importlib.util import module_from_spec, spec_from_file_location


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source', type=Path, required=True)
    ap.add_argument('--apply', action='store_true')
    args = ap.parse_args()
    source = args.source.resolve()
    manifest = json.loads((source/'release.json').read_text())
    root = Path(manifest['target'])
    app = Path('/Applications/Firefox.app')
    if not manifest.get('system_layout') or root.parent != Path('/usr/local/lib/siliconforest/firefox'):
        raise ValueError('Expected a new versioned system-layout runtime')
    desktop = Path('/usr/share/applications/org.mozilla.firefox.desktop')
    state = Path('/private/var/db/desktop-state/chonkstep')
    pins = state/'dock'
    preferences = Path.home()/'Library/Preferences/com.apple.LaunchServices.plist'
    if any(p.exists() or p.is_symlink() for p in [app, root, desktop]):
        raise FileExistsError('Application, runtime and desktop entry must all be absent')
    old_pins = pins.read_bytes() if pins.exists() else b''
    ids = old_pins.decode().splitlines()
    # An empty persisted list means the desktop's default strip. Materialize
    # that list before adding Firefox so its other tiles remain visible.
    defaults = Path('/usr/share/chonkstep/dock-pins')
    inherited_defaults = not any(value.strip() for value in ids) and defaults.is_file()
    if inherited_defaults:
        ids = defaults.read_text().splitlines()
    firefox_pin_existed = any(value.strip() == 'org.mozilla.firefox' for value in old_pins.decode().splitlines())
    ids = [value for value in ids if value.strip() != 'org.mozilla.firefox']
    # The requested neighbouring tiles must also exist if the persisted strip is empty.
    for item in ['com.apple.Terminal', 'com.apple.ActivityMonitor']:
        if item not in [v.strip() for v in ids]:
            ids.append(item)
    position = max(i for i, value in enumerate(ids) if value.strip() in ['com.apple.Terminal','com.apple.ActivityMonitor'])+1
    ids.insert(position, 'org.mozilla.firefox')
    new_pins = ('\n'.join(ids)+'\n').encode()
    old_preferences = preferences.read_bytes() if preferences.exists() else None
    settings = plistlib.loads(old_preferences) if old_preferences is not None else {}
    handlers = settings.get('LSHandlers', [])
    if not isinstance(handlers,list) or not all(isinstance(row,dict) for row in handlers):
        raise ValueError('Unrecognized LaunchServices handlers')
    settings['LSHandlers'] = [row for row in handlers if str(row.get('LSHandlerURLScheme','')).lower() not in ['http','https']]
    settings['LSHandlers'] += [dict(LSHandlerURLScheme=scheme,LSHandlerRoleAll='org.mozilla.firefox') for scheme in ['http','https']]
    entry = Path(__file__).with_name('org.mozilla.firefox.desktop').read_bytes()
    entry = entry.replace(b'/Contents/MacOS/firefox\n',b'/Contents/MacOS/firefox-sf\n')
    plan = dict(application=str(app),runtime=str(root),desktop=str(desktop),pins=str(pins),
                resulting_pins=new_pins.decode(),preferences=str(preferences),
                original_bundle=manifest['original_bundle'],state='installed-pending-dock-qualification',
                canonical_frameworks_replaced=False,shared_providers='app-specific pending TERM adoption')
    print(json.dumps(plan,indent=2),flush=True)
    if not args.apply:
        return
    if os.uname().sysname != 'Darwin':
        raise RuntimeError('System installation requires SiliconForest')
    cold = json.loads((source/'cold-verified.json').read_text())
    if not cold.get('cold_before_execution') or cold['destination'] != str(source):
        raise ValueError('Missing matching cold transfer receipt')
    # This tree has not executed. Verify again BEFORE copying to final locations.
    for name,want in cold['sha256'].items():
        if digest(source/name) != want:
            raise ValueError('Staging changed: '+name)
    for name,want in cold['symlinks'].items():
        if str((source/name).readlink()) != want:
            raise ValueError('Staging link changed: '+name)
    token = uuid.uuid4().hex
    staging_app = app.with_name('.sffx-firefox-stage-'+token)
    root.parent.mkdir(parents=True,exist_ok=True)
    shutil.copytree(source,root,symlinks=True,ignore=shutil.ignore_patterns('Firefox.app','original','cold-verified.json'))
    app.parent.mkdir(parents=True,exist_ok=True)
    shutil.copytree(source/'Firefox.app',staging_app,symlinks=True)
    original = staging_app/'Contents/Resources/Mozilla.app'
    shutil.copytree(source/'original/Firefox.app',original,symlinks=True)
    verified = {};verified_links = {}
    for tree,prefix in [(root,''),(staging_app,'Firefox.app/'),(original,'original/Firefox.app/')]:
        for path in tree.rglob('*'):
            if path.is_symlink():
                key = prefix+str(path.relative_to(tree))
                if key in cold['symlinks']:
                    if str(path.readlink()) != cold['symlinks'][key]:
                        raise ValueError('Final cold link mismatch: '+str(path))
                    verified_links[key] = str(path.readlink())
                continue
            if not path.is_file() or path.is_symlink() or (tree == staging_app and path.is_relative_to(original)):
                continue
            key = prefix+str(path.relative_to(tree))
            if key in cold['sha256']:
                actual = digest(path)
                if actual != cold['sha256'][key]:
                    raise ValueError('Final cold hash mismatch: '+str(path))
                final_path = app/path.relative_to(staging_app) if path.is_relative_to(staging_app) else path
                verified[str(final_path)] = actual
    if len(verified) != len(cold['sha256']) or len(verified_links) != len(cold['symlinks']):
        raise ValueError('Final tree did not account for every cold source file/link')
    spec = spec_from_file_location('owner_install',root/'owner-install.py')
    module = module_from_spec(spec);spec.loader.exec_module(module)
    write = module.atomic_write
    backup = root/'activation-backup'
    backup.mkdir(mode=0o700)
    if pins.exists():
        (backup/'dock').write_bytes(old_pins)
    if old_preferences is not None:
        (backup/'LaunchServices.plist').write_bytes(old_preferences)
    record = dict(plan,token=token,installed_at=datetime.now(timezone.utc).isoformat(),
                  pins_before=ids[:position]+ids[position+1:],added_pins=[] if firefox_pin_existed else ['org.mozilla.firefox'],
                  inherited_default_pins=inherited_defaults,
                  preferences_existed=old_preferences is not None,pins_existed=pins.exists(),
                  desktop_content=entry.decode(),final_cold_files=verified,final_cold_links=verified_links)
    (staging_app/'Contents/siliconforest-install.json').write_text(json.dumps(dict(runtime=str(root),token=token))+'\n')
    (root/'installation.json').write_text(json.dumps(record,indent=2)+'\n')
    staging_app.rename(app)
    write(desktop,entry)
    write(pins,new_pins)
    write(preferences,plistlib.dumps(settings))
    print(json.dumps(dict(installed=True,cold_verified_files=len(verified),receipt=str(root/'installation.json'))))


if __name__ == '__main__':
    main()
