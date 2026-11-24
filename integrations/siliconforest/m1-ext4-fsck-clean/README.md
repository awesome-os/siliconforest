# ext4: a filesystem Linux's e2fsck accepts after every halt (issue 61)

Kernel 48 (and 49, the same kernel under the new names) leaves three things on the disk that Linux
`e2fsck -fn` rejects. `fsck-clean.patch` corrects them; `../m1-kernel50/build-kc.nix` is kernel 48
with that one member rebuilt.

## What was wrong

1. **`bg_itable_unused` computed from a released buffer.** `ext4_alloc_inode_locked()` took the count
   of never-used inodes from the inode bitmap after `ext4_meta_bwrite()` had released the bitmap's
   buffer. Since the journal read cache (`../m1-ext4-journal-cache`), a read served from the open
   transaction returns a buffer marked invalid, and XNU's `buf_brelse()` frees the storage of an
   invalid metadata buffer: the count came from freed memory, and the descriptor said "the whole
   inode table is unused" for a group full of files. The driver never reads the field and recomputes
   every group at mount, so nothing showed on the running system. Linux does read it: `e2fsck` and
   `e2image` skip the "unused" inodes (on the M1, 17 November 2026: five groups, 24,170 inodes, the
   root inode among them: "Root inode not allocated", rc 12), and a read-write mount by Linux would
   zero that part of the inode table (`ext4_init_inode_table`: no group of a filesystem written only
   by this driver carries `INODE_ZEROED`).
2. **Blocks left past the end of a file by a write that ran out of space.** `ext4_vnop_write_impl()`
   backs a write in steps of 64 blocks and, when a step failed part way, wrote only the whole steps:
   the blocks the failed step had already allocated stayed behind the end of the file
   (`e2fsck`: "i_size is N, should be N", rc 4). Found by the `fill` step below, not seen on the M1.

3. **An indexed directory that stops being indexed keeps its index root.** Directories that Linux
   made and indexed (HTREE: the larger system directories of the install) lose the index flag when
   this driver changes them (`ext4_dir_drop_htree()`), but block 0 stayed an index root: ".", ".."
   and the index behind it, with no checksum tail (interior index blocks likewise). e2fsck:
   "directory corrupted", "directory has no checksum"; Linux's kernel refuses such a block
   (`ext4_dirblock_csum_verify`: no space for the leaf checksum; read from its source, not run).
   This driver read them normally. Seen on the M1 after the update of 17 November (two directories,
   salvaged by `e2fsck -fy` on a snapshot without loss).

## The patch

- the count is taken while the buffer is still held (`ext4_subr.c`);
- the downgrade to read-only (the halt) and the unmount recompute all groups before their last
  commit (`ext4_jbd.c`), so what Linux reads does not depend on which allocation came last;
- a write cut short by ENOSPC covers every block that was backed (`ext4_vnops.c`);
- when a directory's index is dropped, every block of it that has no checksum tail gets one: its
  last entry gives up 12 bytes, the old index bytes are cleared (`ext4_vnops.c`). Directories
  that kernel 48/49 already left without a tail are not found again by this (their flag is gone):
  `e2fsck -fy` repairs those.

Nothing of the journal's order, the allocator's choices, the NVMe driver, its write gate, the fence
or the halt gate changes.

## Proof

`python3 test-source.py` (host): the driver's own allocator from the source before and after the
patch against a buffer layer that frees an invalid buffer on release: 8192 before, 0 after. A
control of the source text, not of the kernel.

`python3 run-qemu.py --output <new dir> --phase kernel48|fixed [--runs N]` (QEMU, no M1): a fresh
128 MiB ext4 with the M1 root's features and two directories made and indexed with Linux tools
(`debugfs`, `e2fsck -D`: one with one index level, one with two), and in the guest `fsck-load`
(`fsck-load.c`): `pair` (four creates in one transaction), `htree` (a create, a delete and a rename
in each indexed directory), `small` (2,000 small files, half deleted), `cache` (3,000 writes,
replacements by rename, deletes, deletes of open files), `fill` (to ENOSPC, then 200 more attempts),
`purge` (everything deleted), `orphans` (sealed while a process holds four deleted files open),
`after-orphans` (mounted again and sealed). After every step the guest downgrades the filesystem
to read-only as `/sbin/sf-halt` does and copies it to a second disk; the host runs Linux
`e2fsck -fn` (1.47.4) on each copy.

| step | kernel 48's ext4 | with the patch (13 runs of 13) |
|---|---|---|
| pair | dirty in 7 runs of 7 (rc 12 or 4): invalid unused inodes count, root inode not allocated | rc 0 |
| htree | rc 12 in 3 of 3: directory corrupted, no checksum | rc 0 |
| small | dirty (descriptor again; after `htree` the directory stays corrupted to the end) | rc 0 |
| cache | rc 0 without the `htree` step (4 runs) | rc 0 |
| fill | rc 4 in 4 of 4 without the `htree` step: i_size is N, should be N | rc 0 |
| purge | rc 0 without the `htree` step | rc 0 |
| orphans | dirty | rc 4 (see below) |
| after-orphans | rc 0 without the `htree` step | rc 0 |

The first step that dirties the filesystem with kernel 48's driver is the first one, in every run.
Kernel 48's seven runs: four before the `htree` step existed (96 MiB, seven steps), three with it.
The patch as it is here ran 13 times with all eight steps; an earlier state of it (without the
directory part) ran 13 times clean on the seven steps. One further run gave no result: the harness
did not start the guest command (the system was at its prompt; no test ran); repeated with the
same seed.

`orphans` is recorded, not judged: a filesystem sealed while deleted files are open carries them on
the on-disk orphan list (`s_last_orphan`), which is valid ext4; our next mount and Linux's free
them, `e2fsck -fy` does too, and `e2fsck -n` can only report the list ("Inode N was part of the
orphaned inode list. IGNORED", rc 4). A halt of the M1 with such files open (launchd and the
console shell survive `/sbin/sf-halt`) therefore still shows rc 4 from Linux until something
processes the list.

Limits: an isolated VM and a scratch filesystem; clean seals only. No M1 run, no power-loss or
crash-replay proof. "extent tree could be narrower" notes do not change e2fsck's status.

## Kernel 50

`nix-build ../m1-kernel50/build-kc.nix --no-out-link`. Of the 57 members only `ext4` changes
(`nix-support/kc-strip-proof.json` of the two collections differs in that one bundle); the kernel
and the other 55 kexts are kernel 48's inputs, linked by kernel 48's phase. Packing and the
install kit are as for kernel 48 (`../m1-kernel48/pack-kernel-bin.py`).
