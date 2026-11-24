#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The RAM root of ../../chonkstep-darwin/stage_ramdisk.py, smaller on the wire.

Runs that stager unchanged (same arguments, same checks, same record), then on its output:
  - removes what the running system never opens: /System/Library/Kernels (a DEBUG
    kernel for the virt machine and its 133 MB link archive, 159 MB, 68 MB of the 150 MB the
    launcher sends after gzip);
  - shrinks the file system (BS_RAMDISK_MIB, default 384; 320..480);
  - zeroes every free block, so that space freed here or by the stager's own replacements
    costs nothing to send.
The image is uploaded gzip-compressed at about 11 MB/s and decompressed on the M1 at about
47 MB/s of output, so both the compressed and the full size are seconds of every boot.
tools/pcie_assemble.py uses this in place of the stager when PCIE_STAGER names it. No mount,
no target access; the qualified base image is not written.

The result is kept (artifacts/m1-desktop-010/bs-ramdisk-cache, the last three) under a key of
everything that goes into it: the base image, every staged file's destination and content,
the size, and both stager scripts. A boot that stages the same files again links the kept
image instead of building it (BS_RAMDISK_CACHE=0: always build).
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve()
STAGER = HERE.parents[2] / 'chonkstep-darwin' / 'stage_ramdisk.py'
REMOVE = ('/System/Library/Kernels',)
KEEP_FREE = 32 << 20
KEPT = 3


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def cache_key(argv, size_mib):
    mapping = json.loads(Path(argv[argv.index('--mapping') + 1]).read_text())
    entries = [(e['destination'], digest(e['source']), bool(e.get('executable', False)), e.get('replaces_sha256'))
               for e in mapping['files']]
    extra = [a for a in argv if a.startswith('--') and a not in ('--base', '--mapping', '--output')]
    state = dict(base=digest(argv[argv.index('--base') + 1]), files=entries, size_mib=size_mib,
                 remove=REMOVE, stagers=[digest(STAGER), digest(HERE)], arguments=extra)
    return hashlib.sha256(json.dumps(state, sort_keys=True).encode()).hexdigest()[:32], mapping


def from_cache(entry, output, mapping):
    """Publish a kept image as this boot's: the same bytes, the record naming this boot's sources."""
    record = json.loads((entry / 'ramdisk.record.json').read_text())
    if digest(entry / 'ramdisk.img') != record['sha256']:
        return None
    sources = {e['destination']: str(Path(e['source']).resolve()) for e in mapping['files']}
    for item in record['files']:
        item['source'] = sources[item['destination']]
    record['slim']['from_cache'] = entry.name
    try:
        os.link(entry / 'ramdisk.img', output)
    except OSError:
        shutil.copyfile(entry / 'ramdisk.img', output)
        os.chmod(output, 0o444)
    shutil.copyfile(entry / 'ramdisk.filesystem.log', output.with_suffix('.filesystem.log'))
    output.with_suffix('.record.json').write_text(json.dumps(record, indent=2) + '\n')
    os.utime(entry)
    return record


def to_cache(directory, key, output):
    entry = directory / key
    try:
        entry.mkdir(parents=True)
        os.link(output, entry / 'ramdisk.img')
        for suffix in ('.record.json', '.filesystem.log'):
            shutil.copyfile(output.with_suffix(suffix), entry / ('ramdisk' + suffix))
    except OSError:
        shutil.rmtree(entry, ignore_errors=True)
        return
    for old in sorted((p for p in directory.iterdir() if p.is_dir()), key=lambda p: p.stat().st_mtime)[:-KEPT]:
        shutil.rmtree(old, ignore_errors=True)


def run(argv, log):
    done = subprocess.run(argv, capture_output=True, text=True)
    log.write('$ ' + ' '.join(argv) + '\n' + done.stdout + done.stderr)
    return done


def tree(image, path):
    """Files and directories below path, deepest first, as debugfs names them."""
    listing = subprocess.run(['debugfs', '-R', 'ls -p "%s"' % path, str(image)],
                             capture_output=True, text=True, check=True).stdout
    found = []
    for line in listing.splitlines():
        fields = line.split('/')
        if len(fields) < 7 or fields[5] in ('.', '..', ''):
            continue
        mode, name = fields[2], fields[5]
        if any(c in name for c in '"\\\n'):
            raise ValueError('Unexpected name below ' + path)
        child = path.rstrip('/') + '/' + name
        if mode.startswith('04'):
            found += tree(image, child) + [('rmdir', child)]
        else:
            found.append(('rm', child))
    return found


def free_ranges(image):
    """(first block, last block) of every free extent, from the group descriptors."""
    text = subprocess.run(['dumpe2fs', str(image)], capture_output=True, text=True, check=True).stdout
    ranges = []
    for line in re.findall(r'^[ \t]+Free blocks: (.*)$', text, re.M):
        for item in filter(None, (part.strip() for part in line.split(','))):
            first, _, last = item.partition('-')
            ranges.append((int(first), int(last or first)))
    return ranges


def header(image):
    text = subprocess.run(['dumpe2fs', '-h', str(image)], capture_output=True, text=True, check=True).stdout
    return {key: int(re.search(r'^%s:\s+(\d+)$' % key, text, re.M)[1])
            for key in ('Block count', 'Free blocks', 'Block size')}


def main():
    argv = sys.argv[1:]
    output = Path(argv[argv.index('--output') + 1]).absolute()
    size_mib = int(os.environ.get('BS_RAMDISK_MIB', '384'))
    if not 320 <= size_mib <= 480:
        raise ValueError('BS_RAMDISK_MIB is 320..480')
    cache = output.parent.parent / 'bs-ramdisk-cache'
    key, mapping = cache_key(argv, size_mib)
    if os.environ.get('BS_RAMDISK_CACHE', '1') == '1' and (cache / key / 'ramdisk.record.json').is_file() \
            and not output.exists():
        record = from_cache(cache / key, output, mapping)
        if record is not None:
            print(json.dumps({k: record[k] for k in ('status', 'bytes', 'sha256')}))
            return
    subprocess.run([sys.executable, '-B', str(STAGER)] + argv, check=True)
    record_path = output.with_suffix('.record.json')
    record = json.loads(record_path.read_text())
    before = dict(bytes=record['bytes'], sha256=record['sha256'], free_bytes=record['free_bytes'])
    # The stager published a read-only hard link; work on a private copy and replace it.
    work = output.with_suffix('.slim')
    work.write_bytes(output.read_bytes())
    with output.with_suffix('.filesystem.log').open('a') as log:
        log.write('\n# stage_ramdisk_slim.py\n')
        removed = []
        for path in REMOVE:
            removed += tree(work, path) + [('rmdir', path)]
        batch = output.with_suffix('.slim-commands')
        batch.write_text(''.join('%s "%s"\n' % item for item in removed))
        done = run(['debugfs', '-w', '-f', str(batch), str(work)], log)
        batch.unlink()
        bad = [line for line in (done.stdout + done.stderr).splitlines()
               if line and not line.startswith(('debugfs: ', 'debugfs '))]
        if done.returncode or bad:
            raise ValueError('debugfs removal reported: ' + '; '.join(bad[:3]))
        # debugfs rm leaves the group summaries stale; the forced check repairs only those
        done = run(['e2fsck', '-fy', str(work)], log)
        if done.returncode not in (0, 1):
            raise ValueError('e2fsck after the removal: exit %d' % done.returncode)
        if run(['resize2fs', str(work), '%dM' % size_mib], log).returncode:
            raise ValueError('resize2fs failed')
        if run(['e2fsck', '-fn', str(work)], log).returncode:
            raise ValueError('The slim RAM root does not check clean')
        info = header(work)
        block = info['Block size']
        zeroed = 0
        with work.open('r+b') as stream:
            for first, last in free_ranges(work):
                if not 0 < first <= last < info['Block count']:
                    raise ValueError('Free extent outside the file system')
                stream.seek(first * block)
                stream.write(bytes((last - first + 1) * block))
                zeroed += last - first + 1
        if zeroed != info['Free blocks']:
            raise ValueError('Free extents do not add up to the free block count')
        if run(['e2fsck', '-fn', str(work)], log).returncode:
            raise ValueError('The slim RAM root does not check clean after zeroing free blocks')
        log.write('removed %d entries, %d free blocks zeroed\n' % (len(removed), zeroed))
    if info['Free blocks'] * block < KEEP_FREE:
        raise ValueError('Less than 32 MiB free in the slim RAM root; raise BS_RAMDISK_MIB')
    for item in removed:
        gone = subprocess.run(['debugfs', '-R', 'stat "%s"' % item[1], str(work)],
                              capture_output=True, text=True)
        if 'File not found' not in gone.stdout + gone.stderr:
            raise ValueError('Still present: ' + item[1])
    # Every staged file must still be there with its recorded size.
    for entry in record['files']:
        stat = subprocess.run(['debugfs', '-R', 'stat "%s"' % entry['destination'], str(work)],
                              capture_output=True, text=True).stdout
        size = re.search(r'Size: (\d+)', stat)
        if not size or int(size[1]) != entry['bytes']:
            raise ValueError('Staged file changed: ' + entry['destination'])
    with work.open('rb') as stream:
        sha = hashlib.file_digest(stream, 'sha256').hexdigest()
    record.update(bytes=work.stat().st_size, sha256=sha, free_bytes=info['Free blocks'] * block,
                  slim=dict(stager_output=before, removed=[path for _, path in removed],
                            size_mib=size_mib, free_blocks_zeroed=zeroed))
    os.chmod(work, 0o444)
    os.replace(work, output)
    record_path.write_text(json.dumps(record, indent=2) + '\n')
    if os.environ.get('BS_RAMDISK_CACHE', '1') == '1':
        to_cache(cache, key, output)
    print(json.dumps({k: record[k] for k in ('status', 'bytes', 'sha256')}))


if __name__ == '__main__':
    main()
