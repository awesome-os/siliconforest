# Bound ext4 clustered-write allocations at the real EOF

Read-only Linux checks across two clean M1 installer visits found initialized
extents past EOF in Firefox profile files. The new startup-cache case has EOF
52208 and blocks13..15 past EOF. Its logical4KiB blocks complete a16KiB VM page.
No filesystem repair or native fix has been performed by this directory.
Private inode/path/fsck receipts stay under runs/sftb/firefox-install-0044.

The installed ext4 SDK resolves to11f479q… and its frozen source ssrsapdr…;
the mutable upstream checkout is different and is not the installed baseline.
XNU cluster_io explicitly rounds ordinary I/O to PAGE_SIZE, expects BLOCKMAP to
return only the allocation covering real data, and stops once non_rounded_size
is covered. Frozen ext4 instead allocates the complete rounded request. A write
at49152 with EOF52208 therefore allocates blocks12..15 rather than just12.

The narrow eof.patch caps VNODE_WRITE blockmap requests at
max(e_size,e_new_size), including an extending write in progress. The final
filesystem block still covers its valid bytes and padding; a_run lets cluster_io
stop without requesting the unallocated page tail. Reads are unchanged. Existing
bad extents are not freed or repaired. Journal/cache-flush ordering and all NVMe
write/halt fences are unchanged. The documented lock order is mount -> node ->
allocation; the existing node-size helper is called in that order. Inspection of
the frozen node-lock regions finds no inversion across the mount lock.

python3 test-source.py applies the actual patch to exact bytes and compiles the
literal blockmap function and existing EOF helper under ASan/UBSan. An allocation
recorder exposes the frozen13..15 counterexample as an expected assertion failure.
Patched current/growing EOFs, existing extents, read holes, EOF/freed guards and
XNU's literal rounding/tail-stop fragments pass. This is request-length/control
logic with a simulated allocation recorder, NOT a real extent tree, journal,
concurrency, power-loss, filesystem-cleanliness or native runtime proof.

build-ext4.nix freezes the qualified SDK recipe and hashes before/after the one
source file. Native SDK compilation succeeds with96 exported symbols identical to the
qualified driver. Real QEMU filesystem rehearsal now passes: the frozen driver produces nine
initialized-tail i_size errors (including52208 ->65536) and e2fsck -fn RC4;
the fixed driver gives RC0. Both complete28 mmap/write sizes and7 resize steps,
cleanly unmount, and all29 persisted files match an independent host byte oracle.
The first classifier expected different fsck wording and remains a failed run;
its raw counterexample was preserved before correcting the exact size predicate.
Private receipts: runs/sftb/ext4-eof-qemu-{001,002}. This is actual isolated VM
filesystem behavior, not M1, concurrency or power-loss qualification.

build-qemu-image.nix replaces only ext4 in the ordinary virt image. run-qemu.py
creates disposable scratch images and never contacts an M1. The QEMU IO helper
is compiled from eof-io.c; a separate native mode refuses the wrong runtime
kernel UUID, existing directories or any path outside its strict own prefix.
Its read/write/mmap/truncate body is shared with the VM test; native guard-mode
compilation succeeds, native execution is pending.

build-kc.nix prepares one ext4 replacement over qualified44, requires exactly
that module UUID/source to change, retains55 other kext source inputs and core,
and keeps the existing Force-CMO/clone API/overlap proofs. Speaker extra cap
remains0; boot arguments, no-AVE cohort and rollback rules are unchanged.
The collection builds successfully:57 executable modules, only
com.apple.filesystems.ext4 differs from44 (9 changed/48 unchanged versus29),
all55 other kext source paths identical, zero segment overlap and the real
Force-CMO and devfs link checks pass. The first verifier mistakenly matched
the separate RavynOS compatibility ID as well; its failure is retained before
requiring the exact ext4 filesystem ID. The candidate is installed normal as45 (capsule5c323fb6/prelink2BFF4349),
with44 rollback. Native28 IO cases+7 resizes and29 host byte oracles pass.
The initial data-only st_blocks assertion FAIL is retained: five fragmented
files include extent metadata. map-tail.py uses the exact read-only
F_LOG2PHYS_EXT ABI/BLOCKMAP flags0;247 initialized blocks and681 hole/unwritten
tail blocks through128KiB pass. This does not inventory unwritten preallocation
or establish physical-media readback/whole-root cleanliness. GL/Vulkan5 pass,
cap0/import/ring2 and standard desktop respond; UART/all locks released03:45:05.
Private receipts: runs/sftb/firefox-install-0045. No speed improvement is claimed;
the descriptive postboot fsync/unlink sample is worse and needs stage timing.
Linux fsck repairs remain
outside the current bitmap-only permission for these extent cases.

FXPERF also measured4KiB fsync/unlink near14–15ms. That timing is a separate
question: the installed journal already batches at64 metadata blocks/1s except
explicit sync, so a universal per-unlink commit cannot be inferred. Commits do
perform ordered journal and checkpoint flushes; no barrier/durability reduction
is proposed. Measure those stages before selecting a performance change.

The next latency measurement must separate mount-lock acquisition, dirty-page
push, journal writes, first device flush, checkpoint writes and final flush,
using one owned writer and recording transaction block counts. A before/after
30-file4KiB sample can detect a regression but cannot attribute latency across
boots/background work. Do not remove barriers based on that sample. The source
also ignores writeback/commit/flush errors in fsync/fullfsync, and the journal
commit cleanup drops its buffers on error; that is a separate failure-contract
audit requiring injected-failure/VM tests, not an established native I/O fault
or a change in this EOF candidate. No mounted owner filesystem fault injection.

Original filesystem/XNU licences remain in their patched sources. These new
build/host-fixture helpers are MIT. Source patches do not redistribute firmware,
private captures or filesystem identifiers.


Follow-up offline fragmented control: --fragmented seeds alternating holes in a
fresh regular scratch image using debugfs, requires a clean precheck, then boots
the same frozen/fixed drivers. Both28 IO cases+7 resizes/29 dumped byte oracles
pass;17 files in each contain counted extent-tree metadata. With
e2fsck -fn -E no_optimize_extents the frozen image has real initialized EOF
extent errors (RC4), fixed image is clean (RC0). The optional optimization
exclusion matches M1 checks and does not suppress consistency errors. The first
fragmented classifier expected only i_size wording and remains failed; the
corrected check ties either exact i_size or logical13..15 bounds errors to the
inode of /m-52208. Private receipts: runs/sftb/ext4-eof-qemu-fragmented-{001,002}.

A further native45 CLI sample (no input/install/reboot; other desktop work active)
gives three30-file p50s: fsync16.129/13.264/12.678ms and
unlink18.619/13.081/13.489ms. No causal44/45 performance claim follows.
Read-only NVMe counters show0 errors/timeouts, flush lifetime mean6.519ms;
392 flushes during the samples imply an approximate interval mean6.760ms
(from rounded cumulative averages). Global background I/O is included. The
source's two journal flushes could account for much of the13ms latency; this
is a measurement-supported lead, not per-call attribution. DTrace was absent
from PATH. Before choosing an optimization, obtain syscall/commit/flush-stage
counts and timings; preserve crash consistency and all existing barriers.

### Follow-up after the kernel 47 Wi-Fi priority

The kernel 46 Linux precheck has additional initialized EOF/i_size findings
following the observed root ENOSPC episode. They are not all pre-existing
findings, and no repair was performed. Kernel 47 retains the exact kernel 45
filesystem input; its Wi-Fi change is not a filesystem correction.

Review of the actual frozen `ext4_vnop_write_impl` identifies two failure-path
leads to test in a disposable VM before proposing another filesystem change:

- `ext4_alloc_range` can allocate some blocks and then fail within a group of
  at most 64. `mapped_end` advances only after the whole group succeeds. If
  the first group fails, the writer goes directly to `out`, leaving those
  allocations outside unchanged `e_size`. This is a source counterexample
  candidate for the zero-sized files with initialized blocks seen after ENOSPC.
- If `cluster_write` fails after extending, UBC size is restored to the old
  size, but `e_new_size` clears only when it is at most `e_size`. A failed
  extension can therefore retain the intended size and allocated tail. A
  partial copy also needs explicit persisted-size/accounting verification.

Neither lead establishes the cause of any particular M1 inode. Next controls:
use the literal writer with allocator/cluster failure injection, then fresh
small scratch filesystems with controlled free-block exhaustion, first-group
and later-group failures, short copies, truncate/close/remount, independent
byte/extent oracles and `e2fsck -fn`. Never fill or inject errors into the
owner's root. Preserve successful full-write behavior and journal barriers.

FXNET separately reports 100-byte buffered appends: p50 about 52 us but p90
13 ms and p99 24–33 ms on kernel 46. The frozen mount lock can commit on its
outermost unlock; `ext4_jnl_should_commit` checks overflow, 64 metadata blocks
or one-second transaction age. Existing transaction blocks are updated in
place, so a commit on every append cannot be inferred. Before optimizing,
measure write preallocation, mount-lock wait, cluster write, inode logging,
commit decision, journal/checkpoint writes and both flushes separately. The
reported append tail and prior fsync/unlink times are measurements; the
responsible stages remain unmeasured. No durability reduction is proposed.
