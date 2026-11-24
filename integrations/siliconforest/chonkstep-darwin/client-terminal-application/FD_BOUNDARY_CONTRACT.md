# PTY child pre-exec descriptor verification — application revision002

The original216de1cd application's19 files and receipt remain unchanged outside
this isolated revision. Its root-built09643f14 executable and all earlier
criteria/receipts remain separate. No SDK build or target execution is performed
by this source task. Root and an independent reviewer decide whether the later
source-bound target observations meet DESIGN004 DS4; this document does not
declare that requirement satisfied.

The production shell remains fixed `/bin/sh -i +m`, TERM=dumb. CLI, title,
Wayland/XKB/SHM/grid, focus behavior and CHONKTERM/1 READY/CLEANUP are unchanged.
No shell selection, command option, configuration or new inherited FD is added.

## Checked child boundary

Parent obtains the existing `_SC_OPEN_MAX` bound before fork. The accepted
range remains3–1048576, without clamping, substitution or silent widening.
Its own exec-status write FD must fall within `[3,bound)`. Child uses this exact
same bound for its close sweep and subsequent verification. Nothing here claims
coverage for a descriptor already open above that current bound.

After the existing close sweep, before any success status or execve, child
queries **every** fd in `[0,bound)` with real `fcntl(F_GETFD)`:

- 0/1/2 must exist and have FD_CLOEXEC clear after the checked slave dup2.
- The one owned exec-status write pipe must exist and have FD_CLOEXEC set.
- Every other fd must report EBADF. A live extra or any other query error,
  including EINTR, emits a checked setup failure and exits without exec.

Only stack values and existing async-signal-safe syscalls are used below fork;
there is no allocation, destructor, filesystem open or new process. A failed
close is not assumed to have closed anything: the actual subsequent query
decides whether this boundary can succeed. No unexpected FD is repaired or
hidden by a second sweep.

The private internal PTY16-byte status stream adds one FD_CHECKED(stage9)
record, whose value is the exact exclusive bound and whose group field is the
owned exec-status pipe FD. Parent checks both against its own pre-fork values.
That record must occur exactly once before existing READY(stage10); duplicate,
wrong-bound/wrong-pipe, missing or reordered records refuse setup/exec readiness.
Existing session/ctty and subsequent exec-error handling stay intact. This
internal child protocol change does not alter the session's CHONKTERM protocol.
The private parser also makes a partial error record at EOF sticky: even after
FD_CHECKED and READY, trailing incomplete bytes cannot become exec success.

## Private observations and their limits

`child-ready.json` and final `result.json` expose these separate observations:

- `pre_exec_fd_check_observed`: the validated internal FD_CHECKED record arrived.
- `pre_exec_fd_scan_bound_exclusive`: its exact bound, or null when unobserved.
- `exec_status_pipe_eof_observed`: actual EOF read on the CLOEXEC exec-status pipe.
- `shell_exec_checked`: the complete checked setup/FD-check/pipe-EOF convention.

The pre-exec fact may remain true if a later exec error occurs. EOF alone does
not create an FD-check fact or success gate. The fd fact describes the immediate
pre-exec sweep, not an after-exec loader/shell FD inventory. The conventional
exec-error pipe cannot by itself distinguish successful exec from termination
in the narrow post-setup/pre-exec window; a later target run must also bind the
actual shell/PTY prompt and process outcome, not rely on EOF alone.

Host verification uses a real forked PTY child, actual fcntl queries, internal
status records, real exec and output without needing Linux `/proc`. A deliberate
test-only leaked slave FD must fail before FD_CHECKED/exec. Compiling source
mutants that skip the close sweep or verification must be rejected by the real
positive/leak fixtures respectively. Existing host `/proc` checks remain separate
Linux-only supporting evidence. Host tests establish none of Darwin execution,
hardware input, a full sandbox or final DS4 admission.

Build-007 note: SHM pool descriptors (POSIX shared memory, or the fallback file) are opened
after the PTY child was forked, and are close-on-exec: the terminal checks FD_CLOEXEC on the
`shm_open` descriptor and sets it if the platform did not. The child boundary above is unchanged.
