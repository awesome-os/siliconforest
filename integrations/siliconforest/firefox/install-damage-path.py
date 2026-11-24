#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Select FXPERF2 CGL 0303 and damage/cache; plan unless --apply.

Run on the M1 under gui/core/bench locks. --off disables both launcher flags;
--rollback additionally restores the previous OpenGL inode's saved bytes.
--render-thread on|off selects TERM13's already-installed render thread;
--render-thread rollback restores its previous launcher/manifest only.
Existing applications are never signalled.
"""
import argparse
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import tempfile

ROOT = Path('/usr/local/lib/siliconforest/firefox/160-system1')
OPENGL = Path('/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL')
BACKUP = ROOT / 'maintenance-damage-path-v4-sf'
LAUNCHER = Path('/Applications/Firefox.app/Contents/MacOS/firefox-sf')
CANDIDATE_SHA = '7fb34d4ea17b68ccd3e62f568aefadd86d81d5a06b389467aef1f1d7e0e04084'
BASELINE_SHA = '2f792a7fdd452f6e5f8159b534d5cecf5efdc925584ec0c4247d67cd48c16c3e'
RENDER_BACKUP = ROOT / 'maintenance-render-thread-t13-v1-sf'
TERM13 = {
    '/System/Library/Frameworks/AppKit.framework/Versions/A/AppKit':
        'dde63492ac6e2f207a4cb1816d663e73ac7950adf3ef1ec135e5d15ca0e0927c',
    '/usr/lib/libWindowServer.dylib':
        'c716364c0cc9b90b4b2f79318e10f54c9c992d121ccd3ee01a6d1617f67dd197',
    '/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore.pd':
        'af3667bff4483fc71911b10e80ecf08d1e42dab25f9d50b79e0cd883cb06af89',
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def image_uuid(data):
    if data[:4] != b'\xcf\xfa\xed\xfe':
        raise ValueError('Expected a thin little-endian 64-bit Mach-O')
    import uuid
    offset = 32
    for _ in range(struct.unpack_from('<I', data, 16)[0]):
        command, size = struct.unpack_from('<II', data, offset)
        if size < 8 or offset + size > len(data):
            raise ValueError('Invalid Mach-O load command')
        if command == 0x1b:
            return str(uuid.UUID(bytes=data[offset+8:offset+24])).upper()
        offset += size
    raise ValueError('Missing LC_UUID')


def atomic(path, data, mode):
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix='.damage-', delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        temporary.chmod(mode)
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def select_render_thread(args, manifest_path, before, manifest, before_launcher):
    if args.off or args.rollback:
        raise ValueError('Render-thread and damage actions are separate')
    if args.render_thread != 'on':
        receipt = json.loads((RENDER_BACKUP / 'receipt.json').read_text())
        if sha(before_launcher) != receipt['selected_launcher_sha256']:
            raise ValueError('Launcher changed since render-thread selection')
        prior = json.loads((RENDER_BACKUP / 'release.json').read_bytes())
        for key in set(prior) | set(manifest):
            if key not in ['gpu_damage', 'gpu_image_cache', 'gpu_render_thread'] and prior.get(key) != manifest.get(key):
                raise ValueError('Unrelated selection changed: ' + key)
        print(json.dumps(dict(action='render-thread-' + args.render_thread, apply=args.apply)))
        if args.apply:
            if args.render_thread == 'rollback':
                launcher = (RENDER_BACKUP / 'firefox-sf').read_bytes()
                previous = (RENDER_BACKUP / 'release.json').read_bytes()
                if (sha(launcher) != receipt['previous_launcher_sha256'] or
                        sha(previous) != receipt['previous_manifest_sha256']):
                    raise ValueError('Render-thread rollback changed')
                atomic(manifest_path, previous, 0o600)
                atomic(LAUNCHER, launcher, 0o755)
            else:
                updated = dict(manifest, gpu_render_thread=False)
                atomic(manifest_path, (json.dumps(updated, indent=2)+'\n').encode(), 0o600)
        return
    if args.source is None or args.source.parent.parent != Path('/private/var/db/sffx'):
        raise ValueError('Expected fresh owned cold launcher source')
    cold = json.loads((args.source.parent / 'cold-verified.json').read_text())
    launcher = args.source.read_bytes()
    if (cold.get('cold_before_execution') is not True or
            cold.get('destination') != str(args.source.parent) or
            cold.get('sha256', {}).get(args.source.name) != sha(launcher)):
        raise ValueError('Cold launcher verification differs')
    compile(launcher, 'firefox-sf', 'exec')
    if (sha(before_launcher) != 'ff9a465b316e07e31a6b93e4dbbbdbbcc97a7d7df33dac7e40c86503598117af' or
            sha(before) != '6dfea8c660ceb9a6621e1b9f0a4439c16c9f051735439647244ab9771ce0f0ef' or
            RENDER_BACKUP.exists()):
        raise ValueError('Expected unchanged damage-only selection and fresh backup')
    frameworks = {}
    for path, digest in TERM13.items():
        data = Path(path).read_bytes()
        # (the renamed system) TERM13's .pre-t13 copies are old-name builds and were taken away with the
        # rename; the way back from these frameworks is the rename's own restore
        if sha(data) != digest:
            raise ValueError('TERM13 framework/rollback differs: ' + path)
        frameworks[path] = dict(sha256=digest, uuid=image_uuid(data))
    if any(manifest.get(key) is not True for key in ['gpu_presentation', 'gpu_damage', 'gpu_image_cache']):
        raise ValueError('Expected existing GPU/damage/image-cache selection')
    after = (json.dumps(dict(manifest, gpu_render_thread=True), indent=2)+'\n').encode()
    receipt = dict(installed_utc=datetime.now(timezone.utc).isoformat(), frameworks=frameworks,
                   previous_launcher_sha256=sha(before_launcher), selected_launcher_sha256=sha(launcher),
                   previous_manifest_sha256=sha(before), selected_manifest_sha256=sha(after),
                   owner_processes_signalled=False)
    print(json.dumps(dict(receipt, apply=args.apply), indent=2))
    if not args.apply:
        return
    RENDER_BACKUP.mkdir()
    atomic(RENDER_BACKUP / 'firefox-sf', before_launcher, 0o755)
    atomic(RENDER_BACKUP / 'release.json', before, 0o600)
    atomic(RENDER_BACKUP / 'receipt.json', (json.dumps(receipt, indent=2)+'\n').encode(), 0o600)
    shutil.copy2(Path(__file__), RENDER_BACKUP / 'install-damage-path.py')
    try:
        if manifest_path.read_bytes() != before or LAUNCHER.read_bytes() != before_launcher:
            raise RuntimeError('Selection changed while preparing render-thread selection')
        atomic(LAUNCHER, launcher, 0o755)
        atomic(manifest_path, after, 0o600)
        # Keep the previous damage off command usable with this new launcher.
        shutil.copy2(Path(__file__), BACKUP / 'install-damage-path.py')
    except BaseException:
        atomic(manifest_path, before, 0o600)
        atomic(LAUNCHER, before_launcher, 0o755)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--render-thread', choices=['on', 'off', 'rollback'])
    actions = parser.add_mutually_exclusive_group()
    actions.add_argument('--off', action='store_true')
    actions.add_argument('--rollback', action='store_true')
    args = parser.parse_args()
    if os.uname().sysname != 'Darwin':
        raise RuntimeError('Target-only installer')
    with (ROOT / '.graphics-maintenance.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        manifest_path = ROOT / 'release.json'
        before_launcher = LAUNCHER.read_bytes()
        before = manifest_path.read_bytes()
        manifest = json.loads(before)
        if any(manifest.get(key) is not True for key in
               ['system_layout', 'system_ui', 'system_gpu_providers']):
            raise ValueError('Expected the installed canonical UI/graphics cohort')
        if manifest.get('target') != str(ROOT) or manifest.get('diagnostics'):
            raise ValueError('Unexpected installed runtime')
        if args.render_thread:
            select_render_thread(args, manifest_path, before, manifest, before_launcher)
            return
        if OPENGL.is_symlink() or not OPENGL.is_file():
            raise ValueError('Expected canonical regular OpenGL file')
        if args.off or args.rollback:
            receipt = json.loads((BACKUP / 'receipt.json').read_text())
            if sha(before_launcher) != receipt['selected_launcher_sha256']:
                render_receipt = json.loads((RENDER_BACKUP / 'receipt.json').read_text())
                if (sha(before_launcher) != render_receipt['selected_launcher_sha256'] or
                        receipt['selected_launcher_sha256'] != render_receipt['previous_launcher_sha256']):
                    raise ValueError('Launcher changed since promotion')
            if image_uuid(OPENGL.read_bytes()) != receipt['candidate_uuid']:
                raise ValueError('OpenGL changed since this promotion')
            for key, value in json.loads((BACKUP / 'release.json').read_bytes()).items():
                if key not in ['gpu_damage', 'gpu_image_cache', 'gpu_render_thread'] and manifest.get(key) != value:
                    raise ValueError('Unrelated runtime selection changed: ' + key)
            updated = dict(manifest, gpu_damage=False, gpu_image_cache=False)
            print(json.dumps(dict(action='rollback' if args.rollback else 'off', apply=args.apply,
                                  damage=False, image_cache=False, backup=str(BACKUP))))
            if args.apply:
                if args.rollback:
                    old = (BACKUP / 'OpenGL').read_bytes()
                    if sha(old) != receipt['previous_opengl_sha256']:
                        raise ValueError('Rollback file changed')
                    atomic(OPENGL, old, receipt['opengl_mode'])
                    atomic(manifest_path, (BACKUP / 'release.json').read_bytes(), 0o600)
                    atomic(LAUNCHER, (BACKUP / 'firefox-sf').read_bytes(), 0o755)
                else:
                    atomic(manifest_path, (json.dumps(updated, indent=2)+'\n').encode(), 0o600)
            return
        if args.source is None or args.source.parent.parent != Path('/private/var/db/sffx'):
            raise ValueError('Expected OpenGL inside a fresh owned cold staging directory')
        cold = json.loads((args.source.parent / 'cold-verified.json').read_text())
        candidate = args.source.read_bytes()
        if (cold.get('cold_before_execution') is not True or
                cold.get('destination') != str(args.source.parent) or
                cold.get('sha256', {}).get('OpenGL') != CANDIDATE_SHA or sha(candidate) != CANDIDATE_SHA):
            raise ValueError('FXPERF2 rb2 cold identity differs')
        candidate_launcher = (args.source.parent / 'daily-launch.py').read_bytes()
        if sha(candidate_launcher) != cold.get('sha256', {}).get('daily-launch.py'):
            raise ValueError('Cold launcher identity differs')
        compile(candidate_launcher, 'firefox-sf', 'exec')
        previous_opengl = OPENGL.read_bytes()
        if sha(previous_opengl) != BASELINE_SHA or BACKUP.exists():
            raise ValueError('Expected unchanged baseline and unused rollback directory')
        updated = dict(manifest, gpu_damage=True, gpu_image_cache=True)
        after = (json.dumps(updated, indent=2)+'\n').encode()
        receipt = dict(installed_utc=datetime.now(timezone.utc).isoformat(),
                       candidate_cold_sha256=sha(candidate), candidate_uuid=image_uuid(candidate),
                       previous_opengl_sha256=sha(previous_opengl), previous_uuid=image_uuid(previous_opengl),
                       previous_manifest_sha256=sha(before), selected_manifest_sha256=sha(after),
                       previous_launcher_sha256=sha(before_launcher), selected_launcher_sha256=sha(candidate_launcher),
                       opengl_mode=OPENGL.stat().st_mode & 0o777, source=str(args.source),
                       render_thread_enabled=False, owner_processes_signalled=False)
        print(json.dumps(dict(receipt, apply=args.apply), indent=2))
        if not args.apply:
            return
        BACKUP.mkdir()
        atomic(BACKUP / 'OpenGL', previous_opengl, receipt['opengl_mode'])
        atomic(BACKUP / 'release.json', before, 0o600)
        atomic(BACKUP / 'firefox-sf', before_launcher, 0o755)
        shutil.copy2(Path(__file__), BACKUP / 'install-damage-path.py')
        atomic(BACKUP / 'receipt.json', (json.dumps(receipt, indent=2)+'\n').encode(), 0o600)
        try:
            if (manifest_path.read_bytes() != before or OPENGL.read_bytes() != previous_opengl
                    or LAUNCHER.read_bytes() != before_launcher):
                raise RuntimeError('Selection changed while preparing promotion')
            atomic(OPENGL, candidate, receipt['opengl_mode'])
            # Verify canonical bytes on M1 before any new application can execute them.
            if sha(OPENGL.read_bytes()) != CANDIDATE_SHA:
                raise RuntimeError('Canonical cold verification failed')
            atomic(LAUNCHER, candidate_launcher, 0o755)
            atomic(manifest_path, after, 0o600)
        except BaseException:
            atomic(manifest_path, before, 0o600)
            atomic(LAUNCHER, before_launcher, 0o755)
            atomic(OPENGL, previous_opengl, receipt['opengl_mode'])
            raise


if __name__ == '__main__':
    main()
