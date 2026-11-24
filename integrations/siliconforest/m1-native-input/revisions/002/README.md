# IOHID publication revision002

This source-only candidate fixes revision001's early terminal refusal while BSD
has not initialized devfs. The actual010 capture shows both error3 refusals
before BSD root and launchd. The pinned startup can legitimately spend30+30
seconds in IORTC/quiet waits immediately before vfsinit; the old logs do not
measure that duration. See analysis.json for precise evidence and limits.

Apply **publication-readiness.patch after frozen da499 revision001**. Alternatively,
apply **candidate.patch directly to pinned e7aa00a**. These are mutually exclusive
routes to the same three final IOHIDFamily files. Only PDHIDEventQueue.cpp changes
relative to revision001; event-service attachment, header and native005 stay fixed.
Nothing here is applied to a checkout or an existing collection.

The existing single ONCE owner now registers for IOBSD and waits independently
of publication failures. There is one finite180-second readiness watchdog,
bounded60 one-second registration failures, and the original60 publication-failure
attempts begin only after devfs_is_ready(). IOBSD itself is never treated as
success. A false devfs after IOBSD is rechecked until the original watchdog and
then refused. No RTC, timing wait or kernel protection is changed.

Synchronous existing-service notification is safe: callback only sets an atomic
flag and schedules; the owner stores the returned notifier after registration,
then removes it. An after-arm flag check closes the event-before-timer-arm race.
The notifier and all other state remain boot-lifetime; kernel unload is unsupported.

Numeric uptime_ms/elapsed_ms are recorded at BSD wait/ready/refusal and endpoint
publication deferred/ready/refusal transitions. Node/ready logs prove neither
reports nor physical typing. Endpoint permissions remain the existing0666.

Checks (host-only; no SDK, Nix or device):

    python3 -B integrations/puredarwin/m1-native-input/revisions/002/test_publication.py
    python3 -B integrations/puredarwin/m1-native-input/revisions/002/verify_candidate.py

35 tests execute the extracted production queue and source selector with a
fake monotonic clock, ONCE scheduler, devfs and notifications.13 compiling
mutations must fail, including premature publication, lost wake, callback
removal, unbounded readiness/registration and accelerated failure budgets.
These are source/host checks, not an IOKit/kernel ABI or scheduler runtime proof.
Root owns subsequent SDK/KC and host regression; native progression is held.
