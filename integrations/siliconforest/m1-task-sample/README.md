# Owned-child Mach sampling discriminator

The initial FXPERF sampling-death report was WITHDRAWN at04:00:31/04:00:35Z:
its unsampled control showed the same normal Command-Q exit. No Firefox
sampling defect is established. The helper remains a generic Mach API probe. sf-sample suspends the entire task, enumerates threads, obtains states,
walks frames and aggregates its tree while suspended, then resumes the task.
It neither aborts threads nor writes their states. Long suspension, a Mach
state bug and application lifecycle remain distinct hypotheses.

probe.c accepts an exact kernelcache UUID, never an external target PID. It
forks a fresh own child with32 sleeping pthreads per case: enumeration alone,
suspend/enumerate/resume, plus register reads, then plus the actual bounded
frame walk. Each case lasts1s at2ms spacing and reports sample/error counts,
suspension duration and child wait status. No GPU, decoder, desktop or input
is used. SDK compilation and native execution are separate gates; a successful
child test cannot establish Firefox or every wait path is safe.

Native45 pthread-only execution passed all four cases with33 threads each,
zero Mach errors/early deaths and33431 frame reads. Maximum pauses were254us
(enumeration under suspension),576us (register reads) and1563us (frame walk).
This is a small owned-child result, not a Firefox/IPC/GPU qualification.
The optional ipc mode adds16 private Mach receive waiters; it is SDK-built
but unexecuted, and paused while the urgent45 launch-health check proceeds.
No kernel source or collection change is proposed.
The new helper is MIT; the frame walk follows the MIT sf-sample implementation.


The urgent45 launch investigation also adds bootstrap-probe.c/build-bootstrap.nix.
It registers only a newly generated private service name in its SSH process,
exec-spawns its own lookup child, closes the receive right, then unregisters.
The native45 parent/child/cleanup all return0. The initial cleanup-before-close
version returned SERVICE_ACTIVE and stays a failed diagnostic; registration and
child lookup already succeeded there. This tests basic namespace inheritance,
not every Firefox launch or service. Exact44/45 core/boot inputs are unchanged,
and the corrected installed Firefox gate paints3 compositor-route+3 SSH windows.
No kernel patch or rollback follows from the initial isolated launch failures.
