# Native entry signal policy — revision003

This isolated source delta addresses Claude002 E1. Revision002 source, receipt
and both patches remain unchanged. The manager, Shell actions, startup CLI,
application protocol, IPC leaf, fonts and all existing binaries stay unchanged.
This is host-tested source, not a Darwin build, target execution or admission.

The native entry now claims its process signal scope before any runtime files,
listener, UI, HID/PDGOP or application work. It installs checked `sigaction`
actions in this order: SIGPIPE=IGN, SIGCHLD=DFL, then the existing stop-flag
handlers for INT/TERM/HUP. The first two actions have empty handler masks and
flags0, clearing inherited SIGCHLD auto-reap (`SA_NOCLDWAIT`) as well as SIG_IGN.
Stop actions use `SA_RESTART`, preserving the ordinary restart behavior of the
previous `signal()` calls. The pinned PureDarwin libc `gen/FreeBSD/signal.c`
sets that flag unless its private siginterrupt mask requests otherwise; this
standalone entry explicitly chooses restart and does not read that private mask.
The calling thread's blocked-signal mask is not changed.

Each successful replacement captures the entire previous sigaction. Setup
failure rolls back all already installed actions in reverse order and never
enters the session body. On all ordinary returns, the existing session body
finishes its owned-child cleanup attempt and drops listener/display/server/HID/
presenter before these actions are restored in reverse order. Every restoration
is checked; a failure does not skip later restorations or trigger a blind retry.
It forces an error result, and a sticky process flag refuses another managed
entry after uncertain restoration. Entry reentry/concurrency is refused before
changing any disposition or resetting the stop flag.

An outer unwind boundary exists only to restore dispositions on an unexpected
unwind from startup, cleanup, seal or teardown. It does not claim that such an
unwind cleaned up applications or PTYs. The inner frame boundary and owned
cleanup rules remain exactly002. Aborts, OOM, double panics, foreign unwinds and
unreturning syscalls are not covered; external timeout/Linux recovery remains
mandatory. An incomplete app/PTY result stays incomplete even when signals are
successfully restored. No descendant discovery or extra signal is introduced.

Signal dispositions are process-global. The entry guard serializes only this
entry, not arbitrary libraries or outside callers of sigaction. This design is
for the separate single managed desktop process, not an embeddable session in a
process with competing handler owners. Concurrent external replacement is
unsupported. The host tests use local atomics/fake dispositions and pure local
sigaction construction; they never modify the test runner's real dispositions.

A restoration error occurs after the inner session result may already have
been sealed. Therefore qualification must require the actual executable exit0,
not merely `cleanup=Complete` in that inner file. The binary returns nonzero and
prints the separate setup/restore errno details on any signal-policy failure.
Successful restoration is not inferred from an attempted call.

`from-002.patch` applies only to frozen002. `shell.patch` is cumulative from the
same deployed717 baseline. The receipt and verifier bind exact before/after
hashes; the logical full-tree comparison confirms all other source bytes remain
the same. The host fault tests cover all five setup and restore boundaries,
body error/unwind, full-action restoration, poisoned/reentrant admission and
native-entry placement. Seven production-code mutations must fail assertions.

The other002 review gates are unchanged: reserve bootstrap + requested session
seconds +10 seconds cleanup + seal/scheduler margin in the outer budget (E2);
requested startup count with absent actual app/PID/READY records fails external
qualification (E3); the executable is ultimately spawned by its package-bound
path, not an immutable opened executable FD (E4). Ordinary application DS4
revision002 remains a separately owned review/build input with the same public
protocol; this source does not rewrite its evidence or silently admit it.

Root owns native linking. Its reported002 ARM64 build and proposed removal of
an unused CoreFoundation load command do not change this source. Generic fork,
exec and posix_spawn imports can remain in a shared linked dependency; standalone
reachability approval is a source claim, never inferred from symbol absence.
No SDK build, linker change, packet, controller or target operation was made here.
