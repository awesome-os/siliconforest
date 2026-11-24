#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Select a qualified memory-font provider for the installed Firefox only.

Default is a plan. Use a fresh, cold-verified staging tree and a retained actual
renderer qualification. Shared CoreText/AppKit files and owner profiles are
untouched. Apply only under the coordination GUI lock with Firefox closed.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def atomic(path,data,mode=0o600):
    part=path.with_name(path.name+'.font-update-part')
    with part.open('xb') as stream:stream.write(data);stream.flush();os.fsync(stream.fileno())
    part.chmod(mode);os.replace(part,path)


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--source',type=Path,required=True);ap.add_argument('--apply',action='store_true');args=ap.parse_args()
    source=args.source.resolve();root=Path('/usr/local/lib/siliconforest/firefox/160-system1')
    app=Path('/Applications/Firefox.app');manifest_path=root/'release.json';launcher=app/'Contents/MacOS/firefox-sf'
    original=manifest_path.read_bytes();manifest=json.loads(original);installation=json.loads((root/'installation.json').read_text())
    assert json.loads((app/'Contents/siliconforest-install.json').read_text())==dict(runtime=str(root),token=installation['token'])
    assert manifest['target']==str(root) and manifest['system_layout'] and manifest['private_fonts']
    if manifest.get('diagnostics') or manifest.get('coretext_override'):raise RuntimeError('Runtime already has diagnostics or a provider override')
    qualification=json.loads((source/'qualification.json').read_text())
    if not (qualification.get('actual_renderer_reviewed') and qualification.get('web_fonts_enabled') and
            qualification.get('pixel_checks_passed') and qualification.get('hardware_webrender') and
            qualification.get('provider_uuid_matched') and qualification.get('mozilla_unmodified')):
        raise ValueError('Actual browser font/renderer/provider qualification is incomplete')
    destination=root/'overrides/memory-font-v1';backup=root/'maintenance-font-memory-v1'
    if destination.exists() or backup.exists():raise FileExistsError('Provider and maintenance backup must be new')
    plan=dict(runtime=str(root),new_provider=str(destination/'Frameworks'),launcher=str(launcher),backup=str(backup),
              canonical_frameworks_replaced=False,owner_profiles_changed=False,qualification=qualification)
    print(json.dumps(plan,indent=2),flush=True)
    if not args.apply:return
    if os.uname().sysname!='Darwin':raise RuntimeError('Target must be SiliconForest')
    running=subprocess.run(['ps','-axo','command='],capture_output=True,text=True,check=True).stdout
    if any(str(app/'Contents/Resources/Mozilla.app/Contents/MacOS/firefox') in line for line in running.splitlines()):
        raise RuntimeError('Installed Firefox is running; no process signalled')
    cold=json.loads((source/'cold-verified.json').read_text())
    if not cold.get('cold_before_execution') or cold['destination']!=str(source):raise ValueError('Matching cold receipt absent')
    for name,wanted in cold['sha256'].items():
        if sha(source/name)!=wanted:raise ValueError('Cold staging changed: '+name)
    for name,wanted in cold['symlinks'].items():
        if str((source/name).readlink())!=wanted:raise ValueError('Cold staging link changed: '+name)
    backup.mkdir(mode=0o700)
    (backup/'release.json').write_bytes(original);(backup/'firefox-sf').write_bytes(launcher.read_bytes());(backup/'firefox-sf').chmod(0o755)
    destination.parent.mkdir(exist_ok=True)
    shutil.copytree(source/'provider',destination,symlinks=True)
    verified={}
    for path in destination.rglob('*'):
        relative='provider/'+str(path.relative_to(destination))
        if path.is_symlink():
            if str(path.readlink())!=cold['symlinks'][relative]:raise ValueError('Final provider link mismatch')
        elif path.is_file():
            actual=sha(path)
            if actual!=cold['sha256'][relative]:raise ValueError('Final provider cold mismatch')
            verified[str(path)]=actual
    try:
        atomic(launcher,(source/'firefox-sf').read_bytes(),0o755)
        manifest['coretext_override']=str(destination/'Frameworks')
        atomic(manifest_path,(json.dumps(manifest,indent=2)+'\n').encode())
        record=dict(plan,installed_at=datetime.now(timezone.utc).isoformat(),final_cold_provider_files=verified,
                    launcher_sha256=sha(launcher),previous_manifest_sha256=hashlib.sha256(original).hexdigest(),
                    rollback='With Firefox closed, restore release.json and firefox-sf from this maintenance backup; no shared framework changed.')
        (backup/'receipt.json').write_text(json.dumps(record,indent=2)+'\n')
    except BaseException:
        atomic(launcher,(backup/'firefox-sf').read_bytes(),0o755);atomic(manifest_path,original)
        raise
    print(json.dumps(dict(installed=True,provider=str(destination),receipt=str(backup/'receipt.json'))))


if __name__=='__main__':main()
