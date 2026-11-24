# Shared Shell native revision 007: kernel message record after a signal reap

Revision 006 stays immutable. Candidate-003 (006) failed and is not retried.
Its first app, launched alone, was still SIGKILLed before it created
pty-rx.bin. The exit reason was OS_REASON_SIGNAL code 9 with the victim itself
as sender, and the CS flags at spawn were 0x20020001 (no CS_KILL).

`reports/sprint-010/shared-shell-first-app-sigkill-spawnattr-analysis-001.json`
describes a candidate mechanism. The RAM disk libSystem's
`posix_spawnattr_init` leaves the newer spawn-attribute fields uninitialized,
including the port, file-descriptor and kqworkloop hard limits. The kernel
applies any nonzero value it finds there. A hard limit crossed on the child's
own thread makes pinned XNU send SIGKILL through the resource-limit fatal
port, which matches the recorded exit reason. That path logs with os_log,
which goes only to the kernel message buffer, so the serial console cannot
show it.

This revision records that buffer. It changes only `applications.rs` and
`launch_audit.rs` in `crates/wm-darwin`.

## D4: kernel message buffer after a signal reap (diagnostic)

- After the manager reaps an owned application whose wait status is a signal,
  it reads the kernel message buffer once with `proc_kmsgbuf`, using a
  256 KiB buffer. The kernel copies the newest bytes when the log is larger.
  Pinned XNU allows this only for root.
- It keeps only lines containing `caught allocating too many`,
  `Unable to create task token ident object` or `send_resource_violation`.
  These are the pinned os_log lines on the resource-limit fatal-port path. It
  keeps the 8 most recent matching lines, each cut to 240 bytes of printable
  ASCII (other bytes become `?`).
- Records:
  - journal `event=KernelMessages kernel_messages=read euid=N bytes=N matched=N kept=N`
    or `query_failed euid=N errno=N`;
  - one journal line per kept line, `event=KernelMessage index=i text=...`;
  - snapshot `attempt=N kernel_messages=...` (or `not_observed`), plus
    `attempt=N kernel_message[i]=...`.
- What it can show: a line naming `chonk-terminal-d[14]` confirms the
  fatal-port path and says which limit (file descriptors, Mach ports or
  kqworkloops) was crossed and at what count. No matching line, with a
  successful read as root, counts against this mechanism.
- Normal exits and still-running children are never followed by a read.
  Host builds record `unsupported`.

## Unchanged

No code that runs before the first spawn changes. One pre-spawn data size
does change: the per-attempt audit record gains one field, as it did in 005
and 006. `OwnedChild` is created only after the shim returns, so its new field
is allocated after the spawn. The audit vector's first allocation holds four
records, far above the 256-byte malloc class used by the 248-byte spawn
attributes. Changes of the same kind in 005 and 006 did not stop the first app
from dying. Everything else is as in 006, including D1 (exit
reason before reap), D2 (CS flags), B1 (serialized launches), F1 (full cleanup
wait), the CLI, fonts, signals, startup count, C shim binding, application
protocol, export and supervisor.

This is a diagnostic, not a fix. The first app is expected to die again. Any
fix (zeroing the attribute tail in the shim, or a libSystem rebuild) is a
separate reviewed change.

It needs independent source review, a native build, a new candidate packet
with criteria fixed before boot, and one bounded unattended boot with verified
Linux recovery.
