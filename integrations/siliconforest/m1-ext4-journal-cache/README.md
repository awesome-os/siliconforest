# Read pending ext4 journal metadata before issuing a disk read

Kernel 47's small-append latency is variable under the shared desktop. In one
native batch of 300 100-byte writes, every call completed below 0.7 ms; a repeat
had 98 calls above 2 ms, with a 12.3 ms p90 and 29 ms maximum. A preallocated
overwrite control also had stalls. Slow calls consumed tens to hundreds of
microseconds of thread CPU, so these were predominantly waits, not CPU work.
The background was active; these are not controlled performance A/B results.

A bounded 16-stack capture found a concrete wait edge: our append thread was
waiting for ext4's mount mutex, owned by the compositor's thread during
close/inactive/free-inode-extents. The owner was in
`ext4_write_super -> ext4_blkread -> buf_meta_bread -> SFAppleNVMeRW sf_io`,
waiting for synchronous storage I/O. Another snapshot caught our writer doing
synchronous block zero-initialization. Not every stall has been attributed.
This does not establish a journal commit on every write. Existing metadata
writes cache their journal image, while commits batch at 64 blocks or one
second and perform two ordered flushes. The native fsync/unlink sample had
medians about 10.8/11.3 ms. Those calls need their own stage attribution.

One avoidable operation is source-concrete: `ext4_blkread` first reads the
physical metadata block, then overlays its newer pending journal image.
If the authoritative full block is already in the journal transaction, the
disk read is unnecessary. The candidate checks that image first using a
borrowed metadata buffer. On a hit it fills the entire buffer, clears its
error/residual and marks it invalid for release. It issues no synthetic I/O
completion, delayed write, or physical write. On a miss, disabled cache,
absent journal or failed buffer acquisition, the existing read/overlay path
remains. Callers retain the metadata mount lock; bootstrap is single-threaded.
The invalid borrowed buffer is discarded on release, leaving the transaction
image authoritative until the existing checkpoint writes it.

`ext4_subr.c` contains the reader and counters; `ext4_vfsops.c` explicitly
registers/unregisters their sysctls with successful filesystem registration.
The write allocator, EOF fix, journal log/commit,
checkpoint order, both device flushes, NVMe driver and frozen write-gate/fence
files remain unchanged. `kern.sf_ext4.journal_cache` permits runtime 0/1 A/B;
`kern.sf_ext4.cache_stats` exposes aggregate reads, journal hits and physical
bread calls. Counters are atomic; no timer or polling worker is added.

The exact deployed 45/46/47 SDK baseline is asserted. The full SDK build
succeeds. The actual patched reader and unchanged journal overlay compile and
pass ASan/UBSan with a fake I/O provider: exact data/sector units, hit despite
physical read failure, invalid clean buffer, disabled/no-journal/miss,
late-overlay, I/O error and balanced buffer ownership. These are control and
failure tests, not real buffer-cache, journal replay, concurrency or crash
consistency proof. Disposable QEMU validation precedes any M1 install.

The isolated QEMU guest tests both runtime settings, 32 create/read/chmod/
rename/copy/unlink cases each, the existing 28 write/mmap plus seven resize
cases, full independent data oracles and a read-only filesystem check after
clean unmount. Cache counters must show real hits and unchanged hit counts
while disabled. A first controller failed before boot on an already-used
image output name; a second VM booted but failed when the new sysctls were
not registered. Both failures are preserved. Explicit lifecycle registration
was added before a fresh VM rehearsal. The corrected VM passes: disabled hits
delta 0, enabled hits delta 6272, all 64 metadata payloads and the 29 existing
write/resize payloads match independent debugfs byte oracles, USB 8 MiB reads
match and the cleanly unmounted scratch filesystem passes e2fsck -fn with
status 0. The candidate collection has 57 modules, only ext4 changes from
qualified 47, with the core and 56 other UUIDs identical. Kernel 48 was subsequently installed through the normal installer and
qualified on the M1, with exact qualified 47 retained as rollback. Native
results and their limits follow; no general filesystem repair is claimed.

Private evidence: `runs/sftb/dmabuf-fences/` (native baseline/stacks) and
`runs/sftb/ext4-journal-cache/` (host/SDK/VM candidate). The initial stack capture
failed because the Python ctypes variadic syscall signature was undeclared on
arm64; the corrected attempt recorded 0.38–0.68 ms per snapshot. Native stack
buffers and device identifiers remain private.


## Kernel 48 native qualification, 2026-11-16

The normal capsule is `2ec8d7c4…`, runtime UUID
`E042DBE8-9934-4A88-A71A-16DCAD877E05`; qualified 47 (`adad24e9…`)
is rollback. Core and 56 other module UUIDs and the 599-byte command line are
identical to 47. The required notice, GUI/core/bench locks, UART capture and
one-time boot were used. Shutdown reached Linux without an external reset.
The read-only precheck retained the previous extent/i_size/bitmap/count
classes (status 4); no repair occurred and the root is not claimed clean.
The standard DESK10 `.next` desktop started automatically, with responsive
non-input stats, scroll step 0.206 mm and Wi-Fi firmware timeout 2000 ms.
UART reader/route were stopped and all install locks released.

The runtime sequence 0/1/1/0, 1000 100-byte appends and 32 metadata cases per
setting, passed full data checks; invalid setting 2 was rejected and setting 1
restored. Aggregate counters during enabled phases recorded 12197 and 11909
journal hits (disk bread avoided); disabled phases recorded zero hits.
The system background was uncontrolled. All 4000 appends finished below
0.74 ms, including both disabled controls, so the original append stall was
not reproduced and **no append latency improvement is demonstrated**.
The 32-operation metadata-case medians were 15.4/15.9/18.5/18.2 ms in order,
which likewise do not establish a performance gain. The evidence establishes
correct data and use of the shortcut, not a general speedup.

GL hash checks and Vulkan device/render gates passed five times each. Three
desktop and three SSH launches of the installed Firefox painted distinct
local pages and all captures were visually inspected. All owned process
identities disappeared after natural quit in 1.48–1.92 seconds, with no
cleanup signals; SSH launcher status was zero. Desktop-launch exit status
was not available. These are launch/paint/quit gates, not page-load, media,
frame-rate, crash-consistency or resolution of earlier intermittent bootstrap
failures. Four framework/launcher hashes were unchanged; an attempted fifth
QuartzCore.pd path did not exist, so no fifth comparison is claimed.

## Remaining fsync/unlink cost: captured on kernel 48

A separate bounded 64-cycle workload created an owned file, wrote 100 bytes,
called fsync, closed and unlinked it. Median fsync/unlink times were
11.84/11.30 ms (p90 16.65/15.26 ms). Thirty-two active-thread kernel stackshots
were retained privately, with their actual capture cost recorded. Of the
writer's 32 samples, **25 were sleeping in SFAppleNVMeRW's flush path**, called
by `e4j_flush_device -> ext4_jnl_commit`: 16 after the journal commit-record
flush and nine after the checkpoint flush. Exact bundle disassembly maps
return offsets +0x8d8 and +0x988 to those two calls. This sampling fraction is
not a measured wall-time percentage.

Both explicit fsync and unlink were caught in those waits. For unlink, the
captured chain is `unlinkat_internal -> vnode_put -> vnode_reclaim_internal
-> vclean -> VNOP_FSYNC -> ext4_vnop_fsync -> ext4_jnl_commit`. Thus reclaim
can synchronously commit the mount's current journal transaction even when
the application did not request fsync for that unlink. One sample was a
physical group-descriptor metadata read during extent freeing; two others
were mount-lock waits. The fast journal-hit path cannot eliminate the two
commit/checkpoint flushes or a read absent from the pending transaction.

This establishes a concrete source of the reported fsync/unlink delays and
mount-lock amplification, in addition to the earlier metadata-read wait edge.
It does not establish the device's internal cause of each flush latency,
attribution for every original append stall or one commit per ordinary write.
The earlier 40000-append trace had only ten global flushes, including
background work. Next measurements should distinguish flush completion from
queue/lock delay and attribute per-file reclaim commits before proposing
journal batching changes. The frozen write-gate/fence files, journal ordering,
two flushes and NVMe implementation were not modified.

Private native evidence is under `runs/sftb/firefox-install-0048/`, including
cache A/B, stackshots, exact-symbol/disassembly analysis, Firefox captures,
installer receipt, UART and final state. Device logs and identifiers remain
outside committed documentation.
