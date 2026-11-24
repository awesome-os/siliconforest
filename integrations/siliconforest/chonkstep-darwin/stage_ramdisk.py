#!/usr/bin/env python3
"""Add a hash-recorded desktop payload to a separate, disposable ext4 RAM root.

The mapping JSON contains files: [{source, destination, executable?}]. No mount,
target access, persistent disk modification, or source-image write is performed.
Every added file is dumped back and checked before the result is published.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import re
import subprocess
import tempfile

# The qualified base images: the root of upstream's arm64 minimal image at the tip (the default
# since 12 November 2026, artifacts/rebase/ramdisk-tip.img) and at the pinned revision e7aa00a6
# (artifacts/m1-bringup-009/ramdisk.img, for PCIE_PIN=1 boots).
BASE_SHAS = {
    'bd6b06c1cfbd53037f3db3f906af4b5705fdb0147280e24ce4b31590ea8e5a1a': 'tip c713d8fb',
    'b1154b2854c9771445a9b1b631d3d48837b35f655a4747665b94c72da10e4d05': 'pin e7aa00a6',
}
LAB = Path(__file__).resolve().parents[3]


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def quote(value):
    value = str(value)
    if any(c in value for c in ('\n', '\r', '\x00')):
        raise ValueError('Invalid debugfs path')
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--mapping', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--size-mib', type=int, choices=(384, 480), default=480)
    args = parser.parse_args()
    output = args.output.absolute()
    artifacts = (LAB / 'artifacts').resolve()
    if not output.parent.resolve().is_relative_to(artifacts) or output.exists():
        raise ValueError('Require a new output under the lab artifacts directory')
    BASE_SHA = digest(args.base)
    if BASE_SHA not in BASE_SHAS:
        raise ValueError('Baseline RAM root does not match a qualified image')
    mapping = json.loads(args.mapping.read_text())
    files, destinations = [], set()
    for entry in mapping['files']:
        source = Path(entry['source']).resolve(strict=True)
        dest = PurePosixPath(entry['destination'])
        if (not source.is_file() or not dest.is_absolute() or '..' in dest.parts
                or str(dest) in destinations or str(dest) == '/'):
            raise ValueError('Invalid or duplicate payload mapping')
        destinations.add(str(dest))
        replacement = entry.get('replaces_sha256')
        if replacement is not None and (not isinstance(replacement, str) or
                re.fullmatch('[0-9a-f]{64}', replacement) is None):
            raise ValueError('Replacement requires an exact baseline file SHA256')
        files.append(dict(source=str(source), destination=str(dest),
                          executable=entry.get('executable', False),
                          bytes=source.stat().st_size, sha256=digest(source)))
        if replacement is not None:
            files[-1]['replaces_sha256'] = replacement
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='desktop-root-', dir=output.parent) as temp:
        temp = Path(temp)
        image = temp / 'ramdisk.img'
        shutil.copyfile(args.base, image)
        os.chmod(image, 0o600)
        log = temp / 'filesystem.log'
        with log.open('wb') as stream:
            def preserve_failure():
                stream.flush()
                with output.with_suffix('.failed-filesystem.log').open('xb') as failed:
                    failed.write(log.read_bytes())

            def run(argv):
                stream.flush()
                try:
                    subprocess.run(argv, stdout=stream, stderr=subprocess.STDOUT, check=True)
                except subprocess.CalledProcessError:
                    # Preserve filesystem failures even though the unpublished
                    # private image is removed by TemporaryDirectory cleanup.
                    preserve_failure()
                    raise
            run(['e2fsck', '-fn', str(image)])
            run(['resize2fs', str(image), str(args.size_mib) + 'M'])
            # Replacing a baseline runtime file requires observing its exact
            # old bytes in this private image before any removal. Never mutate
            # the qualified source image, and never silently replace a name.
            for index, f in enumerate(files):
                if 'replaces_sha256' not in f:
                    continue
                check = subprocess.run(['debugfs', '-R', 'stat ' + quote(f['destination']), str(image)],
                                       capture_output=True, text=True, check=True)
                stream.write(check.stdout.encode() + check.stderr.encode())
                mode = re.search(r'Mode:\s+([0-7]{3,4})\b', check.stdout)
                owner = re.search(r'User:\s+(\d+)\s+Group:\s+(\d+)\b', check.stdout)
                links = re.search(r'Links:\s+(\d+)\b', check.stdout)
                if (not re.search(r'Type:\s+regular\b', check.stdout) or not mode or not owner or
                        not links or int(links[1]) != 1):
                    raise ValueError('Replacement target must be a singly linked regular file')
                f['replaces_metadata'] = dict(mode=int(mode[1], 8), uid=int(owner[1]),
                                               gid=int(owner[2]), nlink=1)
                f['executable'] = bool(int(mode[1], 8) & 0o111)
                prior = temp / ('baseline-file-' + str(index))
                run(['debugfs', '-R', 'dump ' + quote(f['destination']) + ' ' + quote(prior), str(image)])
                if not prior.is_file() or digest(prior) != f['replaces_sha256']:
                    raise ValueError('Baseline replacement mismatch: ' + f['destination'])
            dirs = sorted({str(p) for f in files
                           for p in PurePosixPath(f['destination']).parents
                           if str(p) != '/'}, key=lambda p: (p.count('/'), p))
            commands = []
            for directory in dirs:
                check = subprocess.run(['debugfs', '-R', 'stat ' + quote(directory), str(image)],
                                       capture_output=True, text=True, check=True)
                stream.write(check.stdout.encode() + check.stderr.encode())
                if re.search(r'Type:\s+directory\b', check.stdout):
                    continue
                if 'File not found by ext2_lookup' not in check.stderr:
                    raise ValueError('Payload parent is not a directory: ' + directory)
                commands.append('mkdir ' + quote(directory))
            for f in files:
                if 'replaces_sha256' in f:
                    commands.append('rm ' + quote(f['destination']))
                metadata = f.get('replaces_metadata', dict(mode=0o755 if f['executable'] else 0o644,
                                                           uid=0, gid=0))
                commands += ['write ' + quote(f['source']) + ' ' + quote(f['destination']),
                             'set_inode_field ' + quote(f['destination']) + ' mode ' +
                             '0' + format(0o100000 | metadata['mode'], 'o'),
                             'set_inode_field ' + quote(f['destination']) + ' uid ' + str(metadata['uid']),
                             'set_inode_field ' + quote(f['destination']) + ' gid ' + str(metadata['gid'])]
            for index, f in enumerate(files):
                commands.append('dump ' + quote(f['destination']) + ' ' + quote(temp / str(index)))
            batch = temp / 'debugfs.commands'
            batch.write_text('\n'.join(commands) + '\n')
            stream.flush()
            batch_start = stream.tell()
            run(['debugfs', '-w', '-f', str(batch), str(image)])
            stream.flush()
            batch_lines = log.read_bytes()[batch_start:].decode('utf-8').splitlines()
            echoed = [line[len('debugfs: '):] for line in batch_lines if line.startswith('debugfs: ')]
            allocated = [line for line in batch_lines if re.fullmatch(r'Allocated inode: \d+', line)]
            unexpected = [line for line in batch_lines if line.strip() and
                          not line.startswith(('debugfs: ', 'debugfs ')) and
                          re.fullmatch(r'Allocated inode: \d+', line) is None]
            if echoed != commands or len(allocated) != len(files) or unexpected:
                preserve_failure()
                raise ValueError('debugfs mutation batch reported an unexpected result')
            for index, f in enumerate(files):
                dumped = temp / str(index)
                if not dumped.is_file() or digest(dumped) != f['sha256']:
                    raise ValueError('Payload dump-back mismatch: ' + f['destination'])
            run(['e2fsck', '-fn', str(image)])
        header = subprocess.run(['dumpe2fs', '-h', str(image)], check=True,
                                capture_output=True, text=True).stdout
        free_blocks = int(re.search(r'^Free blocks:\s+(\d+)$', header, re.M)[1])
        block_size = int(re.search(r'^Block size:\s+(\d+)$', header, re.M)[1])
        if free_blocks * block_size < 16 * 1024 * 1024:
            raise ValueError('Require at least 16 MiB free guest RAM-root storage')
        record = dict(status='RAM_ROOT_STAGED_NOT_BOOTED', base_sha256=BASE_SHA,
                      bytes=image.stat().st_size, sha256=digest(image), files=files,
                      payload_dump_back_verified=True, e2fsck_exit_code=0,
                      free_bytes=free_blocks * block_size,
                      host_mount_performed=False, target_accessed=False)
        os.chmod(image, 0o444)
        # Exclusive publication; preserve an existing candidate on a lost race.
        os.link(image, output)
        output.with_suffix('.record.json').write_text(json.dumps(record, indent=2) + '\n')
        shutil.copyfile(log, output.with_suffix('.filesystem.log'))
    print(json.dumps({k: record[k] for k in ('status', 'bytes', 'sha256')}))


if __name__ == '__main__':
    main()
