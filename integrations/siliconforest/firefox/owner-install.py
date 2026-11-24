#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Prepare or apply the owner's reviewed Firefox integration.

Default prints a concrete plan. --apply requires a qualification record for
this exact private release. For an authorized owner system installation use
install-system.py, which copies the pristine payload into /Applications.
The Cocoa owner separately owns installing the open(1) HTTP(S) patch.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import plistlib
import shutil
import stat
import tempfile


def atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    previous = path.stat() if path.exists() else None
    mode = stat.S_IMODE(previous.st_mode) if previous else 0o644
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix='.sffx-', delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    temporary.chmod(mode)
    if previous:
        os.chown(temporary, previous.st_uid, previous.st_gid)
    os.replace(temporary, path)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--release', type=Path, required=True)
    ap.add_argument('--source', type=Path, help='host staging source for a read-only plan')
    ap.add_argument('--qualification', type=Path)
    ap.add_argument('--home', type=Path, default=Path.home())
    ap.add_argument('--applications', type=Path, default=Path('/Applications'))
    ap.add_argument('--desktop-directory', type=Path, default=Path('/usr/share/applications'))
    ap.add_argument('--state', type=Path, default=Path('/private/var/db/desktop-state/chonkstep'))
    ap.add_argument('--apply', action='store_true')
    args = ap.parse_args()
    release = args.release.resolve()
    source = args.source.resolve() if args.source else release
    manifest = json.loads((source / 'release.json').read_text())
    if str(release) != manifest['target']:
        raise RuntimeError('Release moved; its CGL and sandbox paths are prefix-specific')
    bundle = release / 'Firefox.app'
    info = plistlib.loads((source / 'Firefox.app/Contents/Info.plist').read_bytes())
    if info.get('CFBundleIdentifier') != 'org.mozilla.firefox':
        raise RuntimeError('Unexpected bundle identifier')
    icon = source / 'Firefox.app/Contents/Resources' / info['CFBundleIconFile']
    if not icon.is_file():
        raise FileNotFoundError(icon)
    app_link = args.applications / 'Firefox.app'
    if app_link.exists() or app_link.is_symlink():
        if not app_link.is_symlink() or app_link.resolve() != bundle:
            raise FileExistsError('Refusing to replace existing application: ' + str(app_link))
    desktop = args.desktop_directory / 'org.mozilla.firefox.desktop'
    entry = (source / 'org.mozilla.firefox.desktop').read_bytes()
    if desktop.exists() and desktop.read_bytes() != entry:
        raise FileExistsError('Refusing to replace existing desktop entry: ' + str(desktop))
    pins = args.state / 'dock'
    old_pins = pins.read_bytes() if pins.exists() else b''
    new_pins = old_pins
    if b'org.mozilla.firefox' not in [line.strip() for line in old_pins.splitlines()]:
        new_pins += (b'\n' if new_pins and not new_pins.endswith(b'\n') else b'') + b'org.mozilla.firefox\n'
    preferences = args.home / 'Library/Preferences/com.apple.LaunchServices.plist'
    old_preferences = preferences.read_bytes() if preferences.exists() else None
    settings = plistlib.loads(old_preferences) if old_preferences is not None else {}
    handlers = settings.get('LSHandlers', [])
    if not isinstance(handlers, list) or not all(isinstance(row, dict) for row in handlers):
        raise ValueError('Unrecognized LaunchServices handlers; refused modification')
    settings['LSHandlers'] = [row for row in handlers if str(row.get('LSHandlerURLScheme', '')).lower() not in ['http', 'https']]
    settings['LSHandlers'] += [dict(LSHandlerURLScheme=scheme, LSHandlerRoleAll='org.mozilla.firefox')
                               for scheme in ['http', 'https']]
    qualification = json.loads(args.qualification.read_text()) if args.qualification else {}
    launcher = qualification.get('launcher_run', {})
    delivery = qualification.get('url_run', {})
    expected = str(bundle / 'Contents/MacOS/firefox-sf')
    qualified = bool(qualification.get('target') == str(release) and qualification.get('state') == 'qualified' and
        launcher.get('measurement_passed') and delivery.get('measurement_passed') and delivery.get('integration', {}).get('complete') and
        launcher.get('command', [None])[0] == expected and delivery.get('command', [None])[0] == expected)
    plan = dict(release=str(release), qualified=qualified, application_symlink=dict(path=str(app_link), target=str(bundle)),
                desktop_entry=str(desktop), icon=str(release / 'original/Firefox.app/Contents/Resources' / info['CFBundleIconFile']),
                creates_application=not app_link.is_symlink(), creates_desktop_entry=not desktop.exists(),
                pins=str(pins), append_pin='org.mozilla.firefox',
                resulting_pins=new_pins.decode('utf-8'), desktop_entry_content=entry.decode('utf-8'),
                preferences=str(preferences), set_url_schemes=['http', 'https'],
                resulting_url_handlers=[row for row in settings['LSHandlers'] if row.get('LSHandlerURLScheme') in ['http', 'https']],
                preserves_other_pins=True, preserves_other_defaults=True,
                open_command_prerequisite='Cocoa owner installs the qualified open(1) HTTP(S) patch',
                rollback='Original pins/preferences are backed up in the private release; remove only the new symlink and entry')
    print(json.dumps(plan, indent=2))
    if not args.apply:
        return
    if source != release:
        raise RuntimeError('A staged host plan cannot be applied')
    if os.uname().sysname != 'Darwin':
        raise RuntimeError('Owner integration requires SiliconForest')
    if not qualified:
        raise RuntimeError('This exact release has no qualified launcher and URL-delivery record')
    backup = release / ('activation-backup-' + datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ'))
    backup.mkdir(mode=0o700, exist_ok=False)
    (backup / 'plan.json').write_text(json.dumps(plan, indent=2) + '\n')
    if pins.exists():
        shutil.copy2(pins, backup / 'dock')
    if old_preferences is not None:
        (backup / 'LaunchServices.plist').write_bytes(old_preferences)
    args.applications.mkdir(parents=True, exist_ok=True)
    if not app_link.is_symlink():
        app_link.symlink_to(bundle)
    if not desktop.exists():
        atomic_write(desktop, entry)
    if new_pins != old_pins:
        atomic_write(pins, new_pins)
    if old_preferences is None or plistlib.loads(old_preferences) != settings:
        atomic_write(preferences, plistlib.dumps(settings))
    print(json.dumps(dict(applied=True, backup=str(backup))))


if __name__ == '__main__':
    main()
