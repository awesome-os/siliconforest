# Sprint 008: unattended M1 diagnostic cycles

The M5 now controls the M1 directly over SSH and USB, independently of Maclab.
The reviewed controller completed **three consecutive cycles from one command**
without prompts or intervention between cycles. Each cycle observed the exact
XNU diagnostic checkpoint and matching original parked context, sent one external
reset, and verified a new Linux boot. The M1 was left healthy in Linux.

This qualifies the pinned pre-VM diagnostic under m1n1 HV. Full native XNU
initialization, Darwin userspace, physical guest UART behavior and Apple drivers
remain outside this result.

## Runtime evidence

- [Three unattended cycles](../reports/sprint-008/three-unattended-cycles.json):
  3/3 entry/park and Linux-return observations, one reset each, continuous boot
  UUID hash chain, unchanged kernel and loader. Observed reset-intent-to-return
  intervals were 39.098, 60.304 and 39.116 seconds.
- [Final health](../reports/sprint-008/final-health.json): known Linux kernel,
  candidate004 loader, verified original ESP backup, all three filesystems rw,
  watchdogs at 30 seconds/2 minutes, old services inactive and incomplete latch absent.
- [Forced host timeout](../reports/sprint-008/host-timeout-recovery.json): a
  one-second deadline killed the owned guest worker (exit -9); no successful
  entry was claimed. One external reset and a bounded neighbor refresh returned
  Linux in 39.383 seconds.
- [Independent first-entry review](../reports/sprint-008/first-entry-independent-review.json):
  the raw context independently matched the inspected park, BootArgs pointer,
  x19=3, CPU and masked SPSR, together with the exact console checkpoint.
- [Controller review](../reports/sprint-008/kernel-cycle-independent-review.json):
  reset requires fresh positive m1n1 USB evidence after workers stop; absent
  USB waits for Linux instead. Unknown workers, ambiguous identity and incomplete
  state stop further execution.

The checkpoint is:

```text
HVLOG: DARWIN_LAB_M1_EARLY_CONSOLE checkpoint=entry-before-vm next=park kernel_init=not_started
```

## What changed

The owner approved installing the reviewed stage2 m1n1 variant. It adds a
10-second native USB acquisition window and otherwise preserves the existing
Linux payload byte for byte. The first fallback boot returned normally. Apple
stage1, partitions, firmware and boot policy were not changed. The original
loader has verified backups on the ESP, builder and M5.

The M5 owner enabled its accessory access setting after macOS enumerated m1n1
but blocked its functionality. Device selection now checks the USB VID/PID,
physical location, primary CDC interface and a changed registry/session identity
after RAM handoff. It does not select by filename order.

The bootstrap retains firmware in target RAM, preserves and reparses the
actual BootArgs/ADT, and chainloads the exact pinned m1n1 image before the
frozen guest launcher runs. The actual device tree exposed two non-range
metadata properties; the decoder now validates and preserves them instead of
treating them as memory ranges. All other range checks remain in force.

A single selected network-neighbor refresh restored M5 SSH after some resets.
The underlying network mechanism remains unproven. The controller bounds that
refresh and never weakens host-key checking or adds a second reset.

## Use and validation

See [M1_KERNEL_CYCLE.md](M1_KERNEL_CYCLE.md) for the prepared-M5 command,
fixed hashes, private logging, lock and failure boundaries. The tested controller
is `tools/m1_kernel_cycle.py`, SHA256
`3d274d1b699c0adfa1e38361d8348c3ee7567b854c5361ea80cbe23a18b7e3e6`.

[Host checks](../reports/sprint-008/controller-checks.json) passed 247 Python
tests, 77 C groups, cross-ABI compile checks and six mutation checks. The
controller's 31 host tests and bootstrap's 28 tests also passed on the M5.
These host checks are separate from the actual runtime evidence above.

The standalone Linux halt/recovery qualification utility has 27 M5 host tests;
it is not the controller used for the three kernel cycles. Earlier three
halt/reset runs remain separate evidence.

The qualified workload is this pinned diagnostic. Other kernels and arbitrary
firmware, USB or controller power failures require further qualification. This
work does not establish a complete Apple Silicon Darwin port, GPU support,
IOKit/kext execution on the M1, or safe mapped-memory reclamation.
