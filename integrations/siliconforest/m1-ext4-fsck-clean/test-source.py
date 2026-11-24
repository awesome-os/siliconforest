#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Host control: the driver's inode allocator, before and after fsck-clean.patch, against a
buffer layer that frees an invalid buffer's storage on release (host-uaf.c).

Applies kernel 48's three ext4 patches and then fsck-clean.patch to the tip's tree
(rebase/pd.nix), pastes the real ext4_alloc_inode_locked() and its helpers into the harness and
runs both. Expected: before = 8192 on a journal-cache hit (exit 1), after = 0 (exit 0).
A source-level control only; the QEMU proof is run-qemu.py."""
from pathlib import Path
import os, re, shutil, subprocess, sys, tempfile
here = Path(__file__).resolve().parent; lab = here.parent
tree = Path(sys.argv[1]) if len(sys.argv) > 1 else None
if tree is None:
    out = subprocess.run([str(Path.home() / '.nix-profile/bin/nix-build'), '--extra-experimental-features', 'nix-command flakes', '--no-out-link',
                          '-E', '(import %s/m1-kernel48/build-kc.nix).ext4.src' % lab], capture_output=True, text=True, check=True).stdout.split()
    tree = Path(out[-1])
rel = 'src/Kernel/Extensions/ext4'
def block(s, needle):
    start = s.index(needle); p = s.index('{', start); depth = 0
    for end in range(p, len(s)):
        depth += (s[end] == '{') - (s[end] == '}')
        if not depth:
            return s[start:end + 1]
    raise AssertionError(needle)
results = {}
with tempfile.TemporaryDirectory(prefix='sf-ext4-fsck-clean-') as tmp:
    root = Path(tmp); (root / rel).mkdir(parents=True)
    for f in ('ext4_subr.c', 'ext4_jbd.c', 'ext4_vnops.c', 'ext4_vfsops.c'):
        shutil.copy(tree / rel / f, root / rel / f); os.chmod(root / rel / f, 0o644)
    for patch in (lab / 'm1-ext4-eof/eof.patch', lab / 'm1-ext4-journal-cache/journal-cache.patch'):
        subprocess.run(['patch', '--batch', '--fuzz=0', '-s', '-p1', '-i', str(patch)], cwd=root, check=True)
    for phase in ('before', 'after'):
        if phase == 'after':
            subprocess.run(['patch', '--batch', '--fuzz=0', '-s', '-p1', '-i', str(here / 'fsck-clean.patch')], cwd=root, check=True)
        s = (root / rel / 'ext4_subr.c').read_text()
        helpers = '\n'.join(block(s, 'static ' + sig) if False else block(s, sig) for sig in (
            'int\next4_bitmap_test(', 'void\next4_bitmap_set(', 'static void\next4_gd_add_free_inodes(', 'static void\next4_gd_add_used_dirs(',
            'static void\next4_gd_clear_flag(', 'static void\next4_gd_set_itable_unused(', 'static uint32_t\next4_itable_unused_from_bitmap(',
            'static void\next4_gd_note_inode_used('))
        text = (here / 'host-uaf.c').read_text().replace('@HELPERS@', helpers).replace('@ALLOC@', block(s, 'static int\next4_alloc_inode_locked('))
        (root / (phase + '.c')).write_text(text)
        subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O1', '-g', '-Wall', '-Wno-unused-function', str(root / (phase + '.c')), '-o', str(root / phase)], check=True)
        r = subprocess.run([str(root / phase)], capture_output=True, text=True, timeout=15)
        results[phase] = (r.returncode, r.stdout.split('\n'))
        print(phase, 'rc', r.returncode, ' | '.join(l for l in r.stdout.splitlines()))
    j = (root / rel / 'ext4_jbd.c').read_text()
    freeze = block(j, 'int\next4_jnl_freeze('); unmount = block(j, 'void\next4_jnl_unmount(')
    assert freeze.index('ext4_repair_itable_unused(emp)') < freeze.index('ext4_jnl_commit(emp)')
    assert unmount.index('ext4_repair_itable_unused(emp)') < unmount.index('ext4_jnl_commit(emp)')
assert results['before'][0] == 1 and 'journal_cache_hit=1 bg_itable_unused=8192' in results['before'][1], results
assert 'journal_cache_hit=0 bg_itable_unused=0' in results['before'][1], results
assert results['after'][0] == 0 and 'journal_cache_hit=1 bg_itable_unused=0' in results['after'][1], results
print('PASS: before the patch a journal-cache hit stores 8192, after it 0; freeze and unmount recompute before their commit')
