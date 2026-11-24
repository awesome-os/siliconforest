#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""QEMU scratch ext4 under M1-shaped workloads; Linux e2fsck -fn on a copy after every step.

  run-qemu.py --output <new directory> --phase kernel48|fixed [--runs N] [--mib 128] [--seed S]

kernel48: the ext4.kext of kernel 48/49. fixed: the same with fsck-clean.patch. Each run is one
boot on a fresh filesystem; the guest (qemu-guest.sh) seals the filesystem after every step as
the halt does and copies it to a snapshot disk. Never contacts an M1. A step is clean when
e2fsck -fn returns 0. The "orphans" snapshot is taken with deleted files still open: Linux and
our next mount empty that list, e2fsck -n only reports it, so it is recorded and not judged.
"""
import argparse, json, os, re, subprocess, time
from pathlib import Path
repo = Path(__file__).resolve().parents[3]; here = Path(__file__).resolve().parent
nix = str(Path.home() / '.nix-profile/bin/nix-build')
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True); p.add_argument('--phase', choices=['kernel48', 'fixed'], required=True)
p.add_argument('--runs', type=int, default=1); p.add_argument('--mib', type=int, default=128); p.add_argument('--seed', type=int, default=1)
p.add_argument('--keep', action='store_true', help='keep the snapshot images of clean steps too')
a = p.parse_args()
w = a.output.resolve(); w.mkdir(parents=True, exist_ok=False)
STEPS = ['pair', 'htree', 'small', 'cache', 'fill', 'purge', 'orphans', 'after-orphans']

def command(argv, name, timeout=900, env=None, allowed=(0,)):
    with (w / name).open('wb') as f:
        r = subprocess.run(argv, cwd=repo, stdout=f, stderr=subprocess.STDOUT, timeout=timeout, env=env)
    if r.returncode not in allowed:
        raise RuntimeError(name + ' rc=' + str(r.returncode))
    return r.returncode

def build(file, name, extra=()):
    command(['nice', '-n', '5', nix, '--extra-experimental-features', 'nix-command flakes', str(file), '--no-out-link', '--cores', '10', *extra],
            name, timeout=3600)
    paths = [l for l in (w / name).read_text().splitlines() if l.startswith('/nix/store/') and Path(l).is_dir()]
    if not paths:
        raise RuntimeError('No built output in ' + name)
    return Path(paths[-1])

fixed = 'true' if a.phase == 'fixed' else 'false'
tools = build(here / 'build-load.nix', 'load-build.txt')
kext = build(here / 'build-ext4.nix', 'ext4-build.txt', ('--arg', 'fixed', fixed))
image = build(here / 'build-qemu-image.nix', 'image-build.txt', ('--arg', 'fixed', fixed))
images = list(image.glob('*.img')); assert len(images) == 1, images
summary = {'phase': a.phase, 'ext4': str(kext), 'image': str(image), 'e2fsck': subprocess.run(['e2fsck', '-V'], capture_output=True, text=True).stderr.splitlines()[0],
           'runs': [], 'limits': 'Isolated VM scratch filesystem, clean seals only; no M1 runtime and no power-loss proof'}
for run in range(a.runs):
    seed = a.seed + run; tag = 'run%02d' % run
    fs = w / (tag + '-fs.img'); disk = w / (tag + '-disk.img'); snap = w / (tag + '-snap.img')
    with fs.open('wb') as f:
        f.truncate(a.mib << 20)
    # the features and geometry of the M1's root (dumpe2fs -h), small inode ratio so that groups fill up
    command(['mke2fs', '-q', '-t', 'ext4', '-b', '4096', '-I', '256', '-i', '4096', '-g', '8192', '-L', 'fsckscratch',
             '-O', 'metadata_csum,64bit,metadata_csum_seed,^orphan_file', str(fs)], tag + '-mkfs.txt')
    # two directories made and indexed by Linux tools, as the M1's system directories were: h1 with one
    # index level, h2 (long names) with two. e2fsck -D builds the indexes.
    script = w / (tag + '-htree-commands.txt')
    script.write_text('mkdir /h1\nmkdir /h2\n' + ''.join('write /dev/null /h1/e%05d\n' % n for n in range(1, 401))
                      + ''.join('write /dev/null /h2/e%05d%s\n' % (n, 'x' * 244) for n in range(1, 9001)))
    command(['debugfs', '-w', '-f', str(script), str(fs)], tag + '-htree.txt', timeout=300)
    script.unlink(); (w / (tag + '-htree.txt')).unlink()
    command(['e2fsck', '-fyD', str(fs)], tag + '-index.txt', allowed=(0, 1))
    flags = subprocess.run(['debugfs', '-R', 'htree_dump /h2', str(fs)], capture_output=True, text=True).stdout
    levels = re.search(r'Indirect levels: (\d+)', flags); assert levels and int(levels[1]) >= 1, flags[:300]
    assert 'Indirect levels: 0' in subprocess.run(['debugfs', '-R', 'htree_dump /h1', str(fs)], capture_output=True, text=True).stdout
    command(['e2fsck', '-fn', str(fs)], tag + '-precheck.txt')
    command(['python3', str(repo / 'integrations/siliconforest/m1-storage/tools/dr_qemu_disk.py'), str(fs), str(disk)], tag + '-disk.txt')
    layout = re.search(r'first=(\d+) blocks=(\d+)', (w / (tag + '-disk.txt')).read_text()); assert layout
    first, blocks = map(int, layout.groups()); assert first == 4102 and blocks == a.mib * 256
    fs.unlink()
    with snap.open('wb') as f:
        f.truncate((1 + len(STEPS) * a.mib) << 20); f.write(b'SF-FSCK-SNAPSHOT-DISK\n')
    guest = w / (tag + '-guest.sh')
    guest.write_text((here / 'qemu-guest.sh').read_text().replace('@SEED@', str(seed)).replace('@MIB@', str(a.mib)))
    # a short name: the path of QEMU's control socket has a length limit
    name = 'f' + a.phase[0] + '%07x' % (time.time_ns() // 1000000 % (1 << 28)); env = os.environ.copy()
    env.update(QD_BASE_IMAGE=str(images[0]), DR_QEMU_SINGLE='1', DR_QEMU_KERNEL='debug', DR_QEMU_BOOTARGS=' -s sfqemu_fsck=1',
               DR_QEMU_DISK2=str(snap), DR_QEMU_FILES=str(tools / 'bin/fsck-load') + '=/usr/bin/fsck-load')
    command([str(repo / 'integrations/siliconforest/m1-storage/tools/dr_qemu.sh'), name, str(guest), str(disk), 'rw', '1500'],
            tag + '-run.txt', timeout=1700, env=env)
    qrun = repo / 'runs/m1-storage/qemu-dr' / name; serial = (qrun / 'serial.log').read_bytes(); (w / (tag + '-serial.log')).write_bytes(serial)
    text = serial.decode(errors='replace')
    taken = re.findall(r'FSCK_GUEST snapshot=(\d+) step=(\S+) downgrade=(\d+) copy=(\d+)', text)
    record = {'run': run, 'seed': seed, 'guest_pass': 'QDDESK_PASS' in text, 'load': re.findall(r'FSCK_LOAD [^\r\n]*', text),
              'driver_log': sorted(set(re.findall(r'ext4: (repaired bg_itable_unused[^\r\n]*|orphan cleanup[^\r\n]*)', text))), 'steps': []}
    first_dirty = None
    for index, step, drc, crc in taken:
        index = int(index); image_file = w / ('%s-%d-%s.img' % (tag, index, step))
        with snap.open('rb') as f, image_file.open('wb') as o:
            f.seek((1 + index * a.mib) << 20); o.write(f.read(a.mib << 20))
        out = '%s-%d-%s-fsck.txt' % (tag, index, step)
        rc = command(['e2fsck', '-fn', str(image_file)], out, allowed=tuple(range(0, 16)))
        lines = [l for l in (w / out).read_text().splitlines() if l.strip()]
        kinds = sorted(set(re.sub(r'\d+', 'N', l)[:90] for l in lines if not l.startswith(('Pass ', 'e2fsck ', 'fsckscratch:')) and 'Fix? no' != l.strip()))
        record['steps'].append({'step': step, 'downgrade_rc': int(drc), 'copy_rc': int(crc), 'e2fsck_fn_rc': rc, 'messages': kinds[:12]})
        if rc != 0 and step != 'orphans' and first_dirty is None:
            first_dirty = step
        if rc == 0 and not a.keep:
            image_file.unlink()
    record['first_dirty_step'] = first_dirty; record['complete'] = [s for _, s, _, _ in taken] == STEPS
    summary['runs'].append(record)
    disk.unlink(); snap.unlink()
    for leftover in (repo / 'runs/qemu-dev/disk-root').glob(name + '*'):
        leftover.unlink()
    (w / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(tag, 'guest', 'PASS' if record['guest_pass'] else 'FAIL', 'first dirty step:', first_dirty,
          'rc:', ' '.join('%s=%d' % (s['step'], s['e2fsck_fn_rc']) for s in record['steps']), flush=True)
