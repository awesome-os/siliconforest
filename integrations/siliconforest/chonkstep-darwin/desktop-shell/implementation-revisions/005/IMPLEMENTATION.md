# Shared Shell native revision 005 — exit-reason diagnostic and full cleanup wait

Revision 004 stays immutable. The failed `shared-shell-return` attempt 001 used
004 and must not be retried. This revision changes only `applications.rs`,
`launch_audit.rs` and `desktop.rs` in `crates/wm-darwin`.

## D1: kernel exit reason before reap (diagnostic)

Attempt 001's first application was killed with SIGKILL about 9 ms after
`posix_spawn` returned success, before it wrote anything. The kernel's own exit
reason was lost because the manager reaped immediately.

Before the existing `waitpid(pid, WNOHANG)` reap, the owner now runs a
non-reaping check, `waitid(P_PID, pid, WEXITED|WNOHANG|WNOWAIT)`, with `si_pid`
zeroed first, because XNU leaves `siginfo` untouched when nothing has exited.
When the owned child has exited and is still a zombie, the Apple build reads
`proc_pidinfo(PROC_PIDEXITREASONINFO)` (flavor 24, packed 32-byte structure)
with a payload buffer bounded to 320 bytes. If the kernel's payload does not
fit (`ENOMEM`), it falls back to `PROC_PIDEXITREASONBASICINFO` (flavor 25),
which keeps the namespace, code and flags. Other errors are recorded as
`query_failed errno=N`. `ENOENT` means the kernel recorded no reason.

The reason is recorded once per attempt: one journal line
(`event=ExitReason`, at most 1024 bytes) and one snapshot line
(`attempt=N exit_reason=...`, or `not_observed`). The payload is copied
verbatim as hex for host decoding. For userspace signals, pinned XNU stores the
sender's pid (`KCDATA_TYPE_PID` 0x36) and process name (0x37) there. The reason
never changes the wait status, ownership, signals or outcome. A SIGKILL wait
remains a failure.

Host builds record `unsupported` after the same real non-reaping check. Linux
tests exercise `waitid`/`WNOWAIT`, not Darwin's `proc_pidinfo`.

## F1: wait for every owned application during shutdown

`poll_desktop_shutdown` returned `Incomplete` as soon as one application was
incomplete. In attempt 001 that sealed the session at about 315 ms, abandoning
the second application's TERM, reap and CLEANUP; that application then hit a
broken status pipe. Cleanup now stays `Pending` while any owned application is
not terminal, within the coordinator's existing 10-second bound. `Incomplete`
is reported only once every application is terminal, and a first failure stays
a failure.

## Unchanged

The CLI, fonts, signals, startup count and order, C shim binding, application
protocol, records, export and supervisor are unchanged. Two explicit startup
requests still go back-to-back, so a recurrence of the attempt-001 kill can be
recorded with its reason rather than hidden.

This is not a fix for the attempt-001 SIGKILL and makes no runtime claim. It
needs independent source review, a native build, a new candidate packet, and one
bounded unattended boot with verified Linux recovery.
