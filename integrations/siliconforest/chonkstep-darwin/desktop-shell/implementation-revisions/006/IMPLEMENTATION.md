# Shared Shell native revision 006 — code-signing flags record and serialized launches

Revision 005 stays immutable. Candidate-002 (005) failed in the attempt-001
pattern and is not retried. Its D1 record showed the first application's
SIGKILL carried an OS_REASON_SIGNAL exit reason with kcdata PID 14 and
PROCNAME `chonk-terminal-darwin`: the signal was raised in the victim's own
context through a kernel path without an explicit reason. The leading remaining
candidate is a code-signing page kill (`cs_invalid_page` on a process with
CS_KILL), and the only observed trigger difference is concurrency: two spawns
of the same executable about 6 ms apart.

This revision keeps every 005 behaviour, including the D1 exit reason and the
full cleanup wait. It changes only `applications.rs`, `launch_audit.rs`,
`desktop.rs` and `terminal_launch.rs` in `crates/wm-darwin`.

## D2: code-signing flags (diagnostic)

- Right after `posix_spawn` returns an owned child, the manager records
  `csops(pid, CS_OPS_STATUS)`. Pinned XNU allows CS_OPS_STATUS for any pid, and
  exec has already set the child's flags in the parent's context.
- When the non-reaping exit check sees the child exit, the manager records the
  flags again before reaping, if the kernel still answers for the zombie.
  Otherwise it records `query_failed errno=N`.
- The frontend journals its own flags once at journal adoption.
- Records: journal lines `event=CsStatus phase=Spawn|Exit cs_status=...` and
  `scope=manager self_cs_status=...`, plus a snapshot line per attempt,
  `attempt=N cs_at_spawn=... cs_at_exit=...`.
- What it can show: whether CS_KILL (0x200), CS_HARD (0x100) or CS_VALID (0x1)
  are present, and whether CS_KILLED (0x1000000) appears at exit. If CS_KILL is
  absent at spawn, the code-signing page-kill candidate is excluded.
- Host builds record `unsupported`.

## B1: serialized launches (behaviour test)

The desktop takes the next Shell launch request only after every previously
launched application has reported READY or reached a terminal state. Queued
requests stay with the Shell bridge meanwhile, and are cancelled as before when
the session closes. Two explicit startup requests therefore no longer exec the
same binary back-to-back.

Possible outcomes:

- The first app survives alone and both run: concurrency becomes the leading
  trigger.
- The first app is still killed alone: concurrency is excluded.
- The first app survives but the second exec kills it later: that is recorded.

This is a diagnostic behaviour change, not a fix for the unexplained kernel
SIGKILL. A failed first app still ends the session, so the second request is
then refused and recorded as such.

## Unchanged

The CLI, fonts, signals, startup count, C shim binding, application protocol,
records, export and supervisor are unchanged.

It needs independent source review, a native build, a new candidate packet with
criteria fixed before boot, and one bounded unattended boot with verified Linux
recovery.
