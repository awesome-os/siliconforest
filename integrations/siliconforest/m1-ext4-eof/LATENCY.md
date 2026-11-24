# Filesystem latency: evidence and next measurement

The EOF fix in kernel 45 is a correctness change, not a speed improvement.
The before/after samples differ in boot, cache, allocation and background work;
their worse timings cannot establish its causal performance cost. Kernel 45's
three warm samples still have fsync medians of 12.68–16.13 ms and unlink medians
of 13.08–18.62 ms. No NVMe error or timeout was observed. Lifetime flush mean
was 6.519 ms; an interval estimate was about 6.760 ms, with unrelated I/O included.

The exact frozen source already batches journal work at 64 metadata blocks or
one second. There is a concrete bypass: closed-last-link unlink calls
`ext4_finish_free_inode` and `vnode_recycle`; XNU's reclaim reaches
`vclean(DOCLOSE)` and `VNOP_FSYNC(MNT_WAIT)`. `ext4_vnop_fsync` unconditionally
commits the mount journal. A nonempty commit writes journal data/commit, flushes,
checkpoints, flushes again, and advances the journal superblock. This fits the
observed latency but is not yet per-operation attribution. Skipping reclaim's
commit without auditing freed-block reuse and replay ordering is not justified.

| Rank | Hypothesis | Single confirming or rejecting measurement |
| --- | --- | --- |
| 1 | Recycling a deleted vnode forces a nonempty journal commit and its two flushes. | Count reclaim fsync entries, nonempty commits and their flushes during a bounded closed-last-link unlink group; compare an otherwise identical unlink-while-open group before its final closes. |
| 2 | Durable device flushes account for most of a successful commit's elapsed time. | Measure the two flush intervals inside each successful commit and compare their sum with that commit's total duration. Global NVMe averages alone are insufficient. |
| 3 | Other writers make a caller wait for the mount lock or commit their work. | Measure mount-lock acquisition separately from time held and record transaction block count at commit entry. A small lock interval rejects the lock-wait explanation for that cohort. |
| 4 | Dirty-page push dominates explicit fsync independently of journal flushes. | Measure the existing `ubc_msync` interval and result separately from lock/commit on the same fsync. A short UBC interval rejects this explanation. |
| 5 | The EOF clamp changes write clustering or adds material contention. | Compare frozen and EOF-patched drivers in equivalent fresh disposable VM filesystems, counting write blockmap calls/bytes plus lock time for identical workloads; do not infer this from cross-boot wall time on the owner's root. |

Proposed instrumentation is **not implemented or installed**. If selected, it
should record bounded aggregate count/total/max durations at existing call
boundaries, with monotonic timestamps and atomic updates. Separate empty commit,
nonempty success and error outcomes; distinguish journal and checkpoint flushes
from replay/unmount flushes. Record fsync UBC and commit errors even though the
current caller ignores them. Count pending-free/recycled-vnode fsync separately
from live-file fsync, without pretending that this identifies every caller.
Snapshots must be read-only and explicitly approximate during concurrent writes.
No periodic worker, hot-loop logging, mount-lock acquisition by the reader,
buffer allocation in the measured path or changed error/barrier behavior is needed.

The test client should use only a fresh owned directory, retain each call's
elapsed time/result, and obtain counter snapshots before/after each short group:
create/write/fsync/close, closed-file unlink, unlink-while-open, and final close.
Collect global NVMe snapshots at those boundaries too. Interleave group order
and keep background contamination explicit. Do not introduce a global sync or
disable existing workloads to manufacture isolation. There is no owner-idle
dependency, input injection or Firefox profile mutation in this client.

Before a diagnostic kernel install, apply instrumentation to the exact qualified
EOF driver, run actual-source branch/failure tests and the existing real VM
write/mmap/resize/readback/clean-unmount/e2fsck workload, and build the real SDK.
Require identical persisted bytes and journal/flush call order against the
uninstrumented driver. Failure-injection results are model/VM evidence only;
never inject storage errors into the owner's root. Any change to error
propagation or transaction disposal is a separate filesystem design and review.

One possible diagnostic cycle would replace only the AVD and ext4 modules over
qualified 45, keeping the immutable core and the other 55 modules byte-identical.
The AVD candidate is in `../m1-avd-context-stats/`; its native acceptance needs
independent, owned-context playback counts, not global deltas. This is a plan,
not a ready collection, reboot reservation or commitment to install unreviewed
instrumentation. Apply the existing notice/locks/UART/rollback rules, retain
the full read-only fsck output, restore DESK's current standard desktop, and
require fresh painted installed Firefox launches through both desktop and SSH
before promotion. Roll back on a failed acceptance check. No journal barrier,
NVMe write gate or shutdown ordering change is proposed here.
