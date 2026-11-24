# Darwin managed-spawn whitelist prerequisite — source contract 001

No fixture execution or target build is performed by this source task. Root
owns pinned-SDK production, artifact binding, supervisor, packet and execution.
Parent links the real, separately frozen `../darwin_spawn.c` API; neither a
host spawn substitute nor a Rust Command path can satisfy this prerequisite.

Installed parent and exact CLI:

```
/usr/bin/darwin-spawn-whitelist-parent --nonce LOWERCASE_HEX48
```

It creates a new, owned 0700 directory
`/private/tmp/chonk-spawn-whitelist-LOWERCASE_HEX48`. Existing paths refuse a
run; no previous evidence is overwritten. Record files use exclusive creation,
0600, CLOEXEC and NOFOLLOW. Parent uses only its own pipes, socketpairs,
`/dev/null` sentinels and record directory; no personal files are inspected.

The only executable passed to `chonk_darwin_spawn_v1` is the fixed absolute
`/usr/bin/darwin-spawn-whitelist-child`, with explicit argv:

```
/usr/bin/darwin-spawn-whitelist-child --nonce LOWERCASE_HEX48 --scan-limit N
```

`N` is the actual parent RLIMIT_NOFILE soft limit. An infinite, <=3 or >65536
limit fails preflight rather than reducing the bound or promising whole-table
coverage. Parent records soft/hard limits (including infinity), and the child
independently checks the soft limit and inventories every fd in `[0,N)` before
opening any resource. This is a bounded descriptor observation; it says nothing
about an already-open descriptor above the current soft limit or Mach ports.
Only process-local descriptor roles/counts are exported, never inode/device
identifiers, kernel/object pointers or arbitrary file paths.

Before spawn, parent holds two deliberately **non-CLOEXEC** unrelated owned
descriptors: an opened `/dev/null` FD left before F_SETFD and a separate FD
received through SCM_RIGHTS without MSG_CMSG_CLOEXEC, also left before F_SETFD.
It checks F_GETFD for each and holds both live through the actual shim call.
Selected child stdin/stdout/stderr/status sources and all other owned endpoints
are CLOEXEC. The shim alone selects four child destinations, exactly 0/1/2/3.
An opened/received descriptor paused before fcntl is thus exercised using actual
kernel descriptors, without claiming a parallel race or atomic socket creation.

Parent inspects the full real spawn result even on nonzero return. A positive
PID with child_created=1 transfers once to its sole wait owner, including after
local shim cleanup failure. ECHILD or other ownership loss remains failure;
no signal follows reap/ECHILD. Only positive owned PIDs are signalled. There
is no retry, fallback, unrelated child adoption, detached reaper or waitpid(-1).

Both programs explicitly install and check SIGPIPE-ignore: the shim resets the
child's SIGPIPE default. Every endpoint is nonblocking and all partial reads,
writes, EINTR/EAGAIN, EOF, overflow and broken endpoints have checked finite
loops. Parent multiplexes output while writing input, so output saturation
cannot deadlock a blocked stdin writer. Parent operation budget is 8 seconds;
the child uses its own 6-second budget. Cleanup checks wait before TERM, grants
250ms, checks again before KILL, then permits 1500ms for sole-owner reap.
The external supervisor must reserve this shutdown time and treat a syscall
that stops returning or missing records as failure.

## Exact runtime criterion (fixed before execution)

All conditions below are required. A missing field, malformed/extra byte,
truncated/oversize record, nonzero parent exit, timeout or unknown ownership
cannot qualify. Root must bind the executed parent, child, C shim/header and
SDK/import closure to their frozen artifacts separately.

1. Actual parent uname reports Darwin and arm64; preflight records the real
   finite RLIMIT bound and both ambient FD_CLOEXEC observations as zero.
2. Real shim return is zero, primary/spawn/local cleanup errors are zero,
   child_created=1 with positive PID, four duplicates were made, and requested
   and observed full flags equal the SDK-checked full set 0x400e (including
   0x4000). Both fixture compile-time assertions and runtime checks refuse a
   changed set. These attribute facts alone
   do not satisfy the execution criterion.
3. Actual exec'd child's bounded inventory sees low mask 15, zero unexpected
   FDs in `[4,N)`, its own checked soft limit equals N, and all child failure
   bits are zero. Selected stdio must be FIFOs and fd3 a connected Unix stream;
   all four initial FD_CLOEXEC flags must be zero after dup2.
4. Parent sends exactly `CHONKFD/1 IN HEX48` plus LF to child stdin and closes
   that writer. The child reports success only after exact input through EOF.
   Parent receives exactly `CHONKFD/1 OUT HEX48` plus LF on stdout and
   `CHONKFD/1 ERR HEX48` plus LF on stderr. These exercise all three selected
   stdio descriptors; expected comparisons do not generate their input bytes.
5. On private fd3, the parent receives exactly one <=256-byte LF-terminated frame:
   `CHONKFD/1 RESULT HEX48 CHILD_PID N LOW_MASK EXTRA_FDS FAILURE_BITS FIRST_ERRNO` plus LF.
   Identity/PID/bound must match the owned launch; integers are canonical bounded
   decimal, low mask=15, extras=0, failure bits=0 and first errno=0. EOF on all
   outputs follows. Failure bits retain distinct failed checks, with the first
   actual errno (or explicit validation error) as a diagnostic.
6. The parent obtains actual terminal wait status, exit 0, without TERM/KILL or
   ownership error. Every owned endpoint closes, transcripts sync/close, final
   JSON sync/close, no uncertain received-rights cleanup and the actual parent
   exit is 0. Malformed/truncated/extra rights explicitly set
   `unknown_rights_cleanup` and cannot certify complete cleanup; the identifiable
   canonical first right is adopted before those refusals. A killed/reaped failed probe
   is cleanup evidence only, never whitelist success.

## Runtime records

`result.json` is capped at 12,287 bytes (12,288-byte bounded formatting buffer).
Its schema1 records nonce, fixed child path, checked Darwin/arm64 uname result, numeric resource
limits/infinity flags, actual shim fields and return, ambient non-CLOEXEC facts,
scan bound, transcript byte counts/exact matches, parsed child report, wait and
TERM/KILL facts, and bounded `{stage,errno}` failures. `stdout.bin`, `stderr.bin`
and `status.bin` preserve at most 256 observed bytes each. Overflow is failure,
with that bounded prefix retained; it cannot become success after truncation.
Final-record write/sync/close failure is also reported as a fixed parent stderr
failure line and nonzero exit, since a closed/failed result file cannot certify
itself. Root's capture must retain that stderr and actual parent status.

`qualify.py` validates the closed records plus an externally supplied actual
parent exit code. Its host unit tests use explicitly synthetic records and
source policy inspection only. They establish classifier behavior, not Darwin
execution, native input, a complete sandbox or a desktop milestone. This fixture
does not replace DS4's separate PTY-shell exec descriptor test in the frozen
terminal application.
