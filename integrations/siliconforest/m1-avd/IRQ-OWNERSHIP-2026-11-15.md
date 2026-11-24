# Mailbox/job ownership race: hosted fix, hardware unqualified

A new M1 media check on the existing CIO29 decoder produced149/150 matching
NV12 frame hashes. Frame40 at1.333333333s differed; the other149 hashes and
every timestamp matched the independent Linux reference. The same fixture had
previously passed150/150 in a quiet run. Two other decoder contexts were active;
global counters gained454 jobs,455 completions and one stale event, with no
error, timeout or DART fault. Those counters do not identify which process
produced the stale event. The strict failure is retained in
`runs/sffinch/avkit-real-player-stage-v7/strict-qualification.json`; this is not
an accepted rounding difference or a new bit-exact playback pass.

The pixel hashes were captured during preparation, before transport operations.
The controller's independent no-item, ready-file and failed-file values match
native M5 exactly. See `../finch/AVKit/CONTROLLER-2026-11-15.md` for those scopes.

Source review identified a real attribution gap in `sf_avd_irq`: it consumed
and acknowledged the mailbox word, then took job_lock and fetched current.
Consider an old completed job A and a delayed duplicate PP completion:

1. The IRQ reads the old PP word while it holds no job lock.
2. Another CPU finishes A's caller and publishes/submits B, or the next job in
   the same session, under the job lock.
3. The IRQ takes the lock and reads the new current/job_ctx. The submitted-state
   check succeeds, so the old word incorrectly marks B done.

The upstream Linux handler captures its context before consuming the mailbox.
The port lost that snapshot ordering. Merely comparing context pointers after
the read cannot reject this interleaving, including successive jobs in one
session. The narrow patch takes the existing job lock before mailbox read/ack
and keeps it through attribution; the debug early-return path now unlocks it.
Publication and session destruction already use this lock. No mailbox bits,
VP/PP protocol, timeout, delay, DMA import or cache policy is invented or changed.

`host-test/test_irq_source.py` extracts the actual structs and IRQ function and
compiles them with ASan/UBSan against a recording MMIO/scheduling stand-in.
Competing publication is deferred by the same modeled lock. Restoring the old
lock placement makes both session-switch and same-session-next-job checks fail
with the expected exit1; the corrected source passes all10 cases, including
normal PP, early pending/stale completion, VP submission, absent/mismatched
owner and both early-return unlock paths. This is hosted source execution,
not real interrupt, DMA, kernel-build or hardware evidence. The first test
mutation retained an invalid debug-path unlock and failed compilation; that
harness failure remains recorded before the corrected v2 result.

The normal host runner now includes this source test. Separately, the existing
H264/HEVC/VP9 instruction-stream replays pass900 pictures, no sanitizer errors,
no retained device allocations and deterministic second-run streams; all13
Linux-source identity checks pass at6a08a08e5755. Those are recorded-input and
host-memory facts, not a hardware pixel result or KEXT build.

The frame40 hardware cause and the effect of this patch remain **unproved**.
Mailbox provenance, output-copy timing and CPU/device cache visibility still
need measurement. The raw NV12 byte probe in the AVKit directory retains full
software/hardware outputs and identifies differing byte positions; it does
not mask the old failure or stop other decoders.

No kernel/SDK/collection was built or installed by this work. The kernel owner
received the exact diff for a future deliberately scoped trial. It must be
applied to that owner's frozen installed AVD base alone, preserving ABI and
importcap0; copying the entire current source would also adopt unrelated,
uninstalled external-import/task-lifetime work. Do not blend that prototype
into the IRQ change. The installed known-good non-AVE kernel is retained;
AVE remains paused and this is decoder work only.
