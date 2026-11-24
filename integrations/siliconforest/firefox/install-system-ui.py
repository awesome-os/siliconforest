#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Select system Cocoa providers for the installed, untouched Firefox app.

Default is a reviewable plan. Apply under gui.lock after the coordination gate.
Only our text launcher and runtime manifest change; the previous pair is kept.
No owner-idle wait, provider replacement, profile write or process signal.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.machinery
import importlib.util
import json
import os
from pathlib import Path
import struct
import shutil


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def image_uuid(path):
    # Metadata remains usable after PAC lowering; this is not a cold byte hash.
    with path.open('rb') as stream:
        header = stream.read(32)
        if len(header) != 32 or struct.unpack_from('<II', header) != (0xfeedfacf, 0x100000c):
            raise ValueError('Expected a thin arm64 system provider: ' + str(path))
        count, size = struct.unpack_from('<II', header, 16)
        if size > 1024 * 1024:
            raise ValueError('Invalid Mach-O load command size')
        commands = stream.read(size)
    offset = 0
    for _ in range(count):
        if offset + 8 > len(commands):
            raise ValueError('Truncated Mach-O commands')
        kind, length = struct.unpack_from('<II', commands, offset)
        if length < 8 or offset + length > len(commands):
            raise ValueError('Invalid Mach-O command')
        if kind == 0x1b and length == 24:
            import uuid
            return str(uuid.UUID(bytes=commands[offset + 8:offset + 24])).upper()
        offset += length
    raise ValueError('Provider has no LC_UUID: ' + str(path))


def atomic(path, data, mode):
    temporary = path.with_name(path.name + '.system-ui-part')
    with temporary.open('xb') as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    temporary.chmod(mode)
    os.replace(temporary, path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--system-gpu', action='store_true', help='select qualified canonical IOSurface/OpenGL and system broker too')
    args = parser.parse_args()
    source = args.source.resolve()
    root = Path('/usr/local/lib/siliconforest/firefox/160-system1')
    app = Path('/Applications/Firefox.app')
    launcher = app / 'Contents/MacOS/firefox-sf'
    manifest_path = root / 'release.json'
    previous = manifest_path.read_bytes()
    manifest = json.loads(previous)
    installation = json.loads((root / 'installation.json').read_text())
    if json.loads((app / 'Contents/siliconforest-install.json').read_text()) != dict(
            runtime=str(root), token=installation['token']):
        raise ValueError('App and runtime ownership markers disagree')
    if manifest['target'] != str(root) or not manifest.get('system_layout') or manifest.get('diagnostics'):
        raise ValueError('Unexpected installed runtime or active diagnostics')
    cold = json.loads((source / 'cold-verified.json').read_text())
    if cold.get('cold_before_execution') is not True or cold.get('destination') != str(source):
        raise ValueError('Matching cold staging receipt required')
    if sha(source / 'firefox-sf') != cold['sha256']['firefox-sf']:
        raise ValueError('Staged launcher changed')
    surface_source = source / 'surface-provider'
    surface_destination = root / 'overrides/surface-lookup-v1'
    if surface_source.exists() and not args.system_gpu:
        qualification = json.loads((source / 'surface-qualification.json').read_text())
        image = 'surface-provider/Frameworks/IOSurface.framework/Versions/A/IOSurface'
        if (qualification.get('native_fixed_passes') != 5 or qualification.get('native_old_sigsegv') != 5 or
                qualification.get('sha256') != cold['sha256'].get(image)):
            raise ValueError('Matching native IOSurface race qualification required')
        for path in surface_source.rglob('*'):
            name = str(path.relative_to(source))
            if path.is_symlink():
                if str(path.readlink()) != cold['symlinks'].get(name):
                    raise ValueError('Staged IOSurface link changed')
            elif path.is_file() and sha(path) != cold['sha256'].get(name):
                raise ValueError('Staged IOSurface changed')
        if surface_destination.exists():
            raise FileExistsError('IOSurface update destination must be new')
        manifest['surface_override'] = str(surface_destination / 'Frameworks')
    providers = {name: Path('/System/Library/Frameworks') / (name + '.framework/Versions/A') / name
                 for name in ['AppKit', 'Onyx2D', 'CoreText', 'QuartzCore']}
    providers['libWindowServer'] = Path('/usr/lib/libWindowServer.dylib')
    if args.system_gpu:
        for name in ['IOSurface', 'OpenGL']:
            providers[name] = Path('/System/Library/Frameworks') / (name + '.framework/Versions/A') / name
        if not Path('/usr/libexec/iosurfaced').is_file():
            raise FileNotFoundError('Canonical IOSurface broker is not installed')
        providers['iosurfaced'] = Path('/usr/libexec/iosurfaced')
        manifest['system_gpu_providers'] = True
        manifest.pop('surface_override', None)
        manifest.pop('graphics_override', None)
    uuids = {name: dict(path=str(path), uuid=image_uuid(path)) for name, path in providers.items()}
    if args.system_gpu:
        identity_path = source / 'canonical-gpu-identities.json'
        if sha(identity_path) != cold['sha256'].get(identity_path.name):
            raise ValueError('Canonical GPU identity receipt is not cold-verified')
        expected = json.loads(identity_path.read_text())
        if set(expected['identities']) != {'IOSurface', 'iosurfaced'}:
            raise ValueError('Canonical surface and broker identities are both required')
        for name, identity in expected['identities'].items():
            if uuids[name]['uuid'] != identity['uuid']:
                raise ValueError('Canonical GPU cohort is not installed: ' + name)
    # A future system install can supersede c18; record the actual metadata and
    # verify loaded paths in the browser gate instead of pinning stale hashes.
    manifest['system_ui'] = True
    manifest['coretext_source'] = 'system'
    for field in ['ui_override', 'onyx_override', 'coretext_override']:
        manifest.pop(field, None)
    loader = importlib.machinery.SourceFileLoader('candidate_launcher', str(source / 'firefox-sf'))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    candidate = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(candidate)
    command, environment = candidate.plan(root, [], manifest=manifest)
    paths = environment['DYLD_FRAMEWORK_PATH'].split(':') + environment['DYLD_LIBRARY_PATH'].split(':')
    forbidden = ['appkit', 'quartz', 'onyx', 'coretext', 'windowserver']
    if args.system_gpu:
        forbidden += ['surface', 'gl', 'overrides']
        if command[1] != '/usr/libexec/iosurfaced':
            raise ValueError('Candidate launcher did not select the canonical broker')
    if any(any(part in Path(path).parts for part in forbidden)
           for path in paths):
        raise ValueError('A private Cocoa provider remains selected')
    backup = root / ('maintenance-system-gpu-v1' if args.system_gpu else 'maintenance-system-ui-v1')
    if backup.exists():
        raise FileExistsError('System UI backup already exists; review its receipt')
    plan = dict(runtime=str(root), system_providers=uuids, command=command,
                dyld_framework_path=environment['DYLD_FRAMEWORK_PATH'],
                dyld_library_path=environment['DYLD_LIBRARY_PATH'], backup=str(backup),
                private_cocoa_selected=False, native_browser_verified=False,
                owner_idle_required=False, owner_profiles_changed=False, process_signals=False)
    print(json.dumps(plan, indent=2), flush=True)
    if not args.apply:
        return
    if os.uname().sysname != 'Darwin':
        raise RuntimeError('Target must be SiliconForest')
    backup.mkdir(mode=0o700)
    (backup / 'release.json').write_bytes(previous)
    (backup / 'firefox-sf').write_bytes(launcher.read_bytes())
    (backup / 'firefox-sf').chmod(0o755)
    if surface_source.exists() and not args.system_gpu:
        surface_destination.parent.mkdir(exist_ok=True)
        shutil.copytree(surface_source, surface_destination, symlinks=True)
    try:
        atomic(launcher, (source / 'firefox-sf').read_bytes(), 0o755)
        atomic(manifest_path, (json.dumps(manifest, indent=2) + '\n').encode(), 0o600)
        receipt = dict(plan, installed_at=datetime.now(timezone.utc).isoformat(),
                       launcher_sha256=sha(launcher), previous_manifest_sha256=hashlib.sha256(previous).hexdigest(),
                       rollback='Restore release.json and firefox-sf from this backup with Firefox closed.')
        (backup / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    except BaseException:
        # The new launcher understands the previous manifest. Restore that
        # policy before an older launcher can see fields it does not know.
        atomic(manifest_path, previous, 0o600)
        atomic(launcher, (backup / 'firefox-sf').read_bytes(), 0o755)
        raise
    print(json.dumps(dict(installed=True, receipt=str(backup / 'receipt.json'))))


if __name__ == '__main__':
    main()
