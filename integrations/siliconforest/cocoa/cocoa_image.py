#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Disk images for the Cocoa stream's QEMU runs (arm64 `virt`, the QEMU stream's base).

The QEMU stream's template (integrations/siliconforest/qemu-dev/qd_image.py: upstream's minimal
arm64 image, our kernel collection, boot arguments and slow-changing userland) has a 512 MiB
root with about 150 MiB free. The Cocoa frameworks and a browser do not fit, and a framework
tree is full of symbolic links, which qd_image's run layer does not write. So this makes, on
top of that template and without changing it:

  base    runs/cocoa/base/cocoa-<hash>.img: a copy of the template with the root partition
          and its ext4 grown to --root-mib, plus every --tree (a directory copied to a guest
          path with its symbolic links and executable bits). Cached by a hash of the
          template's name, the trees' source directories and their file lists.
  run     --output: a reflink copy of the base plus the --file / --data items.

qd_image.py is called for the template (its command line) and imported for the partition
table and the run layer (`partitions`, `write_root`). Nothing is mounted: the root is edited
with debugfs at the partition's offset, grown with sfdisk and resize2fs on a copy of the
partition.

  cocoa_image.py --output runs/cocoa/<run>/disk.img --tree /nix/store/...-cocoa-root=/ \\
      --file session=/usr/bin/cocoa-session [--root-mib 3072] [--resolution 1024x768]
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent
LAB = HERE.parents[2]
QD = LAB / 'integrations/siliconforest/qemu-dev'
OUT = LAB / 'runs/cocoa'
sys.path.insert(0, str(QD))
import qd_image  # noqa: E402

SECTOR = 512
BASE_VERSION = 2


def run(argv, **kw):
    return subprocess.run([str(a) for a in argv], check=True, text=True, capture_output=True, **kw)


def template(resolution, extra):
    """The QEMU stream's template, through its own command line (a run image without run
    files is a copy of the template; its path is reported as `template`)."""
    scratch = LAB / 'runs/qemu-dev/cocoa-template/disk.img'
    r = run([sys.executable, '-B', QD / 'qd_image.py', '--output', scratch, '--resolution', resolution, *extra], cwd=LAB)
    info = json.loads(r.stdout.strip().splitlines()[-1])
    scratch.unlink(missing_ok=True)
    return Path(info['template'])


def copy_range(src, src_off, dst, dst_off, size):
    with open(src, 'rb') as a, open(dst, 'r+b' if Path(dst).exists() else 'w+b') as b:
        done = 0
        while done < size:
            n = os.copy_file_range(a.fileno(), b.fileno(), min(size - done, 1 << 30), src_off + done, dst_off + done)
            if n <= 0:
                raise OSError('short copy')
            done += n


def grow_root(image, root_mib):
    """Grow partition 2 (the ext4 root, last on the disk) and its filesystem."""
    (esp_off, esp_size), (root_off, root_size) = qd_image.partitions(image)[:2]
    want = root_mib << 20
    if want <= root_size:
        return
    part = image.with_suffix('.root.tmp')
    part.unlink(missing_ok=True)
    copy_range(image, root_off, part, 0, root_size)
    with open(part, 'r+b') as f:
        f.truncate(want)
    r = subprocess.run(['e2fsck', '-fy', str(part)], capture_output=True, text=True)
    if r.returncode > 3:
        raise SystemExit('e2fsck: ' + (r.stdout + r.stderr)[-1500:])
    run(['resize2fs', part])
    # A new table of the same shape: same GUIDs, names and first sectors, partition 2 longer.
    dump = run(['sfdisk', '-d', image]).stdout
    lines = []
    for line in dump.splitlines():
        if line.startswith(('first-lba', 'last-lba')):
            continue
        if 'start=' in line and int(line.split('start=')[1].split(',')[0]) == root_off // SECTOR:
            head, rest = line.split('size=', 1)
            line = head + 'size=%12d,' % (want // SECTOR) + rest.split(',', 1)[1]
        lines.append(line)
    with open(image, 'r+b') as f:
        f.truncate(root_off + want + (1 << 20))          # room for the backup table
    run(['sfdisk', '--no-reread', '--no-tell-kernel', image], input='\n'.join(lines) + '\n')
    now = qd_image.partitions(image)
    assert now[0] == (esp_off, esp_size) and now[1] == (root_off, want), now
    copy_range(part, 0, image, root_off, want)
    part.unlink()


def tree_manifest(source):
    """[(relative path, kind, detail)] of a directory: 'd', 'l' with target, 'f' with mode bit."""
    source = Path(source)
    out = []
    for dirpath, dirnames, filenames in os.walk(source, followlinks=False):
        rel = Path(dirpath).relative_to(source)
        keep = []
        for d in sorted(dirnames):
            p = Path(dirpath) / d
            if p.is_symlink():
                out.append((str(rel / d), 'l', os.readlink(p)))
            else:
                out.append((str(rel / d), 'd', ''))
                keep.append(d)
        dirnames[:] = keep
        for f in sorted(filenames):
            p = Path(dirpath) / f
            if p.is_symlink():
                out.append((str(rel / f), 'l', os.readlink(p)))
            elif p.is_file():
                st = p.stat()
                out.append((str(rel / f), 'f', '%d:%d:%d' % (st.st_size, st.st_mtime_ns, 1 if st.st_mode & 0o111 else 0)))
    return out


def quote(value):
    value = str(value)
    assert not any(c in value for c in '\n\r\x00"'), value
    return '"' + value + '"'


def stage_trees(image, trees):
    """Copy directory trees into the root with one debugfs script. A symbolic link whose
    target is absolute and inside the tree's own source is rewritten relative to the guest."""
    offset, _ = qd_image.partitions(image)[1]
    target = '%s?offset=%d' % (image, offset)
    known = set()

    def exists(path):
        if path in known:
            return True
        r = subprocess.run(['debugfs', '-R', 'stat ' + quote(path), target], capture_output=True, text=True)
        if 'Type:' in r.stdout:
            known.add(path)
            return True
        return False
    commands = []

    def mkdirs(path):
        parts = [p for p in path.split('/') if p]
        for i in range(1, len(parts) + 1):
            d = '/' + '/'.join(parts[:i])
            if not exists(d):
                commands.append('mkdir ' + quote(d))
                known.add(d)
    count = 0
    for source, dest, manifest in trees:
        source = Path(source).resolve()
        dest = '/' + dest.strip('/')
        for rel, kind, detail in manifest:
            guest = (dest.rstrip('/') + '/' + rel) if rel != '.' else dest
            guest = guest.replace('/./', '/')
            if kind == 'd':
                mkdirs(guest)
            elif kind == 'l':
                mkdirs(str(Path(guest).parent))
                link = detail
                if link.startswith(str(source) + '/'):
                    link = dest.rstrip('/') + '/' + link[len(str(source)) + 1:]
                commands += ['rm ' + quote(guest), 'symlink %s %s' % (quote(guest), quote(link))]
            else:
                mkdirs(str(Path(guest).parent))
                executable = detail.endswith(':1')
                commands += ['rm ' + quote(guest), 'write %s %s' % (quote(source / rel), quote(guest)),
                             'sif %s mode 0%o' % (quote(guest), 0o100000 | (0o755 if executable else 0o644))]
                count += 1
    r = subprocess.run(['debugfs', '-w', '-f', '-', target], input='\n'.join(commands) + '\n', capture_output=True, text=True, check=True)
    bad = [l for l in (r.stdout + r.stderr).splitlines()
           if l.strip() and not l.startswith(('debugfs', 'Allocated inode')) and 'File not found by ext2_lookup' not in l]
    if bad:
        raise SystemExit('debugfs: ' + '\n'.join(bad[:12]))
    return count


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--output', type=Path, required=True, help='run image to write (under runs/cocoa)')
    p.add_argument('--tree', action='append', default=[], metavar='DIR=/DEST', help='directory tree for the cached base layer')
    p.add_argument('--file', action='append', default=[], metavar='SOURCE=/DEST', help='file for the run layer (executable)')
    p.add_argument('--data', action='append', default=[], metavar='SOURCE=/DEST', help='file for the run layer (mode 0644)')
    p.add_argument('--dir', action='append', default=[], metavar='/PATH:UID:GID', help='directory to make in the run layer')
    p.add_argument('--root-mib', type=int, default=3072, help='size of the root partition (default 3072)')
    p.add_argument('--resolution', default='1024x768')
    p.add_argument('--qd-arg', action='append', default=[], help='further argument for qd_image.py (e.g. --verbose-boot)')
    a = p.parse_args()
    out = a.output.resolve()
    assert out.is_relative_to(OUT.resolve()), 'run image must be under runs/cocoa'
    tpl = template(a.resolution, a.qd_arg)
    trees = []
    for item in a.tree:
        source, dest = item.split('=', 1)
        trees.append((source, dest, tree_manifest(source)))
    # The source directory is part of the key: files in the Nix store all carry the same
    # modification time, so a rebuilt library of the same size would otherwise look unchanged.
    key = hashlib.sha256(json.dumps([BASE_VERSION, tpl.name, a.root_mib,
                                     [(str(Path(s).resolve()), d, m) for s, d, m in trees]]).encode()).hexdigest()[:16]
    base = OUT / 'base' / ('cocoa-%s.img' % key)
    staged = None
    if not base.exists():
        base.parent.mkdir(parents=True, exist_ok=True)
        tmp = base.with_suffix('.tmp')
        tmp.unlink(missing_ok=True)
        run(['cp', '--reflink=auto', tpl, tmp])
        os.chmod(tmp, 0o644)
        grow_root(tmp, a.root_mib)
        staged = stage_trees(tmp, trees)
        offset, _ = qd_image.partitions(tmp)[1]
        r = subprocess.run(['e2fsck', '-fn', '%s?offset=%d' % (tmp, offset)], capture_output=True, text=True)
        if r.returncode:
            raise SystemExit('e2fsck after staging: ' + (r.stdout + r.stderr)[-1500:])
        tmp.rename(base)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.unlink(missing_ok=True)
    run(['cp', '--reflink=auto', base, out])
    files = {}
    for items, executable in ((a.file, True), (a.data, False)):
        for item in items:
            source, dest = item.split('=', 1)
            files[dest] = (Path(source).resolve(strict=True), executable)
    dirs = [(d.split(':')[0], int(d.split(':')[1]), int(d.split(':')[2])) for d in a.dir]
    if files or dirs:
        qd_image.write_root(out, files, dirs)
    print(json.dumps(dict(image=str(out), base=str(base), template=str(tpl), staged_files=staged,
                          files={d: str(s) for d, (s, _) in files.items()})))


if __name__ == '__main__':
    main()
