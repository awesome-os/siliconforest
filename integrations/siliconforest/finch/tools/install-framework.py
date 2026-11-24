#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Install one reviewed framework, hash cold copies, retain .pre-finch backups.

Run the coordination gate on the workstation immediately before calling this on
the M1. This does not launch programs, replace core libraries, or drive the GUI.
The separate receipt records hashes BEFORE activation; loaded-image readback is
not a pristine-byte check on SiliconForest because the kernel lowers PAC.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import posixpath
import secrets
import shutil
import tempfile


def safe_relative(value):
    p = Path(value)
    if p.is_absolute() or not p.parts or '..' in p.parts:
        raise ValueError('Unsafe framework-relative path')
    return p


def framework_root(root, name):
    if not name.isalnum():
        raise ValueError('Invalid framework name')
    if root.resolve() == Path('/') and os.uname().sysname != 'Darwin':
        raise ValueError('Production install requires Darwin')
    return root/'System/Library/Frameworks'/(name+'.framework')


def write_receipt(path, receipt):
    with path.open('w') as f:
        json.dump(receipt, f, indent=2)
        f.write('\n')
        f.flush()
        os.fsync(f.fileno())


def link_copy(source, target):
    if source.is_symlink():
        target.symlink_to(os.readlink(source))
    else:
        os.link(source, target)


def original_already_preserved(dest, backup):
    if dest.is_symlink() and backup.is_symlink():
        return os.readlink(dest) == os.readlink(backup)
    return (dest.is_file() and not dest.is_symlink() and backup.is_file()
            and not backup.is_symlink() and os.path.samefile(dest, backup))


def rollback(receipt, root):
    base = framework_root(root, receipt['framework'])
    for row in reversed(receipt['activated']):
        dest = base/safe_relative(row['path'])
        backup_rel = safe_relative(row.get('backup_path', str(safe_relative(row['path']).with_name(dest.name+'.pre-finch'))))
        if backup_rel.parent != safe_relative(row['path']).parent:
            raise ValueError('Backup escapes destination directory')
        backup = base/backup_rel
        if row['had_previous']:
            if row.get('previous_directory'):
                if os.path.lexists(dest):
                    dest.unlink()
                os.replace(backup, dest)
                continue
            temporary = dest.with_name(dest.name+'.finch-rollback-'+secrets.token_hex(6))
            link_copy(backup, temporary)
            os.replace(temporary, dest)
        elif os.path.lexists(dest):
            dest.unlink()
    receipt['state'] = 'rolled-back'


def install(manifest, source, root, journal, update=False):
    name = manifest['framework']
    base = framework_root(root, name)
    entries = manifest['entries']
    if journal.exists():
        raise ValueError('Receipt must be new')
    paths = [str(safe_relative(row['path'])) for row in entries]
    if len(paths) != len(set(paths)) or 'Versions/A/'+name not in paths:
        raise ValueError('Duplicate entries or missing main image')
    backups = {}
    update_tag = secrets.token_hex(8)
    # Validate all inputs/backups before changing a target file.
    for row in entries:
        rel = safe_relative(row['path'])
        src, dest = source/rel, base/rel
        if not dest.parent.resolve().is_relative_to(base.resolve()):
            raise ValueError('Destination parent escapes framework')
        backup = dest.with_name(dest.name+'.pre-finch')
        if os.path.lexists(backup) and not original_already_preserved(dest, backup):
            if not update:
                raise ValueError('Original backup already exists: '+str(rel))
            # Preserve the first .pre-finch and this transaction's actual prior
            # provider independently. A failed update restores the latter.
            backup = dest.with_name(dest.name+'.pre-finch-update-'+update_tag)
            if os.path.lexists(backup):
                raise ValueError('Update backup already exists')
        backups[str(rel)] = str(backup.relative_to(base))
        if row['kind'] == 'symlink':
            target = row['target']
            normalized = posixpath.normpath(str(rel.parent/target))
            if os.path.isabs(target) or normalized == '..' or normalized.startswith('../'):
                raise ValueError('External framework symlink')
            if not src.is_symlink() or os.readlink(src) != target:
                raise ValueError('Source symlink differs')
        elif row['kind'] != 'file' or src.is_symlink() or hashlib.sha256(src.read_bytes()).hexdigest() != row['sha256']:
            raise ValueError('Source hash/type mismatch: '+str(rel))
    receipt = dict(framework=name, state='preparing', verified=[], activated=[])
    journal.parent.mkdir(parents=True, exist_ok=True)
    write_receipt(journal, receipt)
    temporary = []
    try:
        for row in entries:
            rel = safe_relative(row['path'])
            src, dest = source/rel, base/rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            if row['kind'] == 'file':
                fd, temp = tempfile.mkstemp(prefix='.finch-cold-', dir=dest.parent)
                temp = Path(temp)
                temporary.append((row, temp, dest))
                with os.fdopen(fd, 'wb') as out, src.open('rb') as inp:
                    shutil.copyfileobj(inp, out)
                    out.flush()
                    os.fsync(out.fileno())
                temp.chmod(src.stat().st_mode & 0o777)
                digest = hashlib.sha256(temp.read_bytes()).hexdigest()
                if digest != row['sha256']:
                    raise ValueError('Cold target hash mismatch')
                receipt['verified'].append(dict(path=str(rel), sha256=digest,
                                                before_activation=True))
            else:
                temp = dest.with_name('.finch-link-'+secrets.token_hex(8))
                temp.symlink_to(row['target'])
                temporary.append((row, temp, dest))
        # Every image has been hashed while unreachable by its public load name.
        receipt['state'] = 'verified-cold'
        write_receipt(journal, receipt)
        main = 'Versions/A/'+name
        temporary.sort(key=lambda item: item[0]['path'] == main)
        for row, temp, dest in temporary:
            previous = os.path.lexists(dest)
            directory = previous and dest.is_dir() and not dest.is_symlink()
            if previous:
                backup = base/backups[row['path']]
                if directory:
                    if row['kind'] != 'symlink':
                        raise ValueError('Cannot replace directory with a regular file')
                    os.rename(dest, backup)
                elif not os.path.lexists(backup):
                    link_copy(dest, backup)
            # Record a moved directory before replacement so rollback can put it
            # back even if the symlink activation itself fails.
            if directory:
                receipt['activated'].append(dict(path=row['path'], had_previous=True, previous_directory=True, backup_path=backups[row['path']]))
                write_receipt(journal, receipt)
            os.replace(temp, dest)
            if not directory:
                receipt['activated'].append(dict(path=row['path'], had_previous=previous, backup_path=backups[row['path']]))
            write_receipt(journal, receipt)
        receipt['state'] = 'installed-awaiting-regressions'
        write_receipt(journal, receipt)
    except BaseException:
        rollback(receipt, root)
        write_receipt(journal, receipt)
        raise
    finally:
        for _, temp, _ in temporary:
            if os.path.lexists(temp):
                temp.unlink()
    return receipt


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root', type=Path, default=Path('/'))
    ap.add_argument('--manifest', type=Path)
    ap.add_argument('--source', type=Path)
    ap.add_argument('--receipt', type=Path, required=True)
    ap.add_argument('--rollback', action='store_true')
    ap.add_argument('--update', action='store_true', help='Keep first originals plus a separate prior-version rollback snapshot')
    args = ap.parse_args()
    if args.rollback:
        receipt = json.loads(args.receipt.read_text())
        rollback(receipt, args.root)
        write_receipt(args.receipt, receipt)
    else:
        if not args.manifest or not args.source:
            ap.error('Install requires manifest and source')
        receipt = install(json.loads(args.manifest.read_text()), args.source, args.root, args.receipt, update=args.update)
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
