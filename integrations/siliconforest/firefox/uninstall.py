#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Remove this Firefox installation, preserving profiles and later owner edits.

Default prints the plan; quit Firefox before --apply. No other app is signalled.
"""
import argparse
import json
from pathlib import Path
import plistlib
import shutil
import subprocess


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--runtime',type=Path,required=True)
    ap.add_argument('--apply',action='store_true')
    args=ap.parse_args();root=args.runtime.resolve()
    if root.parent != Path('/usr/local/lib/siliconforest/firefox'):
        raise ValueError('Unexpected runtime')
    record=json.loads((root/'installation.json').read_text())
    app=Path(record['application']);marker=app/'Contents/siliconforest-install.json'
    if json.loads(marker.read_text()) != dict(runtime=str(root),token=record['token']):
        raise RuntimeError('Application ownership changed')
    print(json.dumps(dict(remove=[str(app),str(root),record['desktop']],
                         preserve='Firefox profiles, logs, other pins and later URL-handler edits'),indent=2))
    if not args.apply:return
    running=subprocess.run(['ps','-axo','command='],capture_output=True,text=True,check=True).stdout
    if any(str(app) in line or str(root/'surface/bin') in line for line in running.splitlines()):
        raise RuntimeError('Quit installed Firefox before uninstalling; no process signalled')
    from importlib.util import module_from_spec,spec_from_file_location
    spec=spec_from_file_location('owner_install',root/'owner-install.py');mod=module_from_spec(spec);spec.loader.exec_module(mod)
    write=mod.atomic_write
    pins=Path(record['pins']);ids=pins.read_text().splitlines() if pins.exists() else []
    # Older receipts also listed the neighbouring Terminal/Activity Monitor
    # tiles. Preserve every other app, including defaults materialized during
    # installation and later owner/desktop changes.
    removable={v for v in record['added_pins'] if v=='org.mozilla.firefox'}
    ids=[v for v in ids if v.strip() not in removable]
    if not ids and not record['pins_existed']:pins.unlink(missing_ok=True)
    else:write(pins,('\n'.join(ids)+('\n' if ids else '')).encode())
    preferences=Path(record['preferences'])
    settings=plistlib.loads(preferences.read_bytes()) if preferences.exists() else {}
    old=plistlib.loads((root/'activation-backup/LaunchServices.plist').read_bytes()) if record['preferences_existed'] else {}
    handlers=settings.get('LSHandlers',[])
    for scheme in ['http','https']:
        matches=[v for v in handlers if str(v.get('LSHandlerURLScheme','')).lower()==scheme]
        if matches and all(v.get('LSHandlerRoleAll')=='org.mozilla.firefox' for v in matches):
            handlers=[v for v in handlers if str(v.get('LSHandlerURLScheme','')).lower()!=scheme]
            handlers += [v for v in old.get('LSHandlers',[]) if str(v.get('LSHandlerURLScheme','')).lower()==scheme]
    settings['LSHandlers']=handlers
    if not record['preferences_existed'] and settings=={'LSHandlers':[]}:
        preferences.unlink(missing_ok=True)
    else:write(preferences,plistlib.dumps(settings))
    desktop=Path(record['desktop'])
    if desktop.exists() and desktop.read_text()==record['desktop_content']:desktop.unlink()
    shutil.rmtree(app);shutil.rmtree(root)


if __name__=='__main__':main()
