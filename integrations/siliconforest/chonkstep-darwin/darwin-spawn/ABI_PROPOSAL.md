# Proposed private Darwin managed-spawn ABI

Proposal only; no implementation or runtime claim. Root owns this leaf; Astra
owns the shared Shell integration. This implements DESIGN002 B2 / DS2-4 rather
than exposing normal Rust Command as a successful fallback.

A pinned-SDK C shim receives a versioned LP64 request and returns a versioned
fixed-width result. Rust sees no opaque SDK attribute/file-action layout.

Request: version1, argc/envc, reserved0, absolute executable, bounded NULL-ended
argv/envp, and exactly four caller-owned source FDs for stdin/stdout/stderr/private
status. Destinations are fixed0/1/2/3. No PATH lookup, shell translation, variable
inherit list, borrowed ambient stdio or pre_exec hook. The status channel uses
only dup2 to3, never addinherit_np. Caller keeps selected sources alive and
prevents close/reuse during the call. The C shim duplicates sources to distinct
owned FDs above3 before constructing actions, so original sources may alias but
source/destination collisions cannot occur. Duplicates are CLOEXEC.

The shim checks all API results, combines CLOEXEC_DEFAULT|SETSIGDEF|SETSIGMASK
(and SETPGROUP if selected) in exactly one setflags call, reads it back, then
calls posix_spawn on the absolute path. No fallback. It sets empty child signal
mask and explicit default TERM/INT/HUP/PIPE/CHLD handling. Failed setup never
spawns; cleanup always attempts all initialized objects and owned duplicates.

Result records stage, returned positive PID only after spawn success, setup and
spawn errors, all local cleanup errors, observed flags and child_created. A
positive returned PID must transfer once to OwnedChild EVEN IF local cleanup
also failed; returning only an error must not lose a living child. Never retry
close after an ambiguous failure or signal an unowned PID. Native runtime
acceptance requires the explicit child FD-whitelist fixture from DESIGN002;
host fake-API tests and target compilation alone do not prove exec inheritance.

Private status channel contract, to coordinate the real terminal: the session
keeps its read side and passes only its write side to child fd3. A new terminal
writes bounded versioned READY and CLEANUP facts with its unique instance ID,
then closes it. No HID bytes, credentials or expected shell response are sent.
Use the bounded fixed ASCII CHONKTERM/1 protocol in
client-terminal-application/APPLICATION_CONTRACT.md, with at most256 bytes per
line includingLF. COMPLETE=1 requires a real wait status, never-1.
Partial/invalid/duplicate/out-of-order messages
fail closed. The manager still owns the PID/wait status; a message alone is not
reap or surface identity. The terminal's later PTY child closes fd3 in its
existing3..maxfd sweep, preserving only its own exec-status pipe. The terminal
may complete writes accepted while focused after focus leaves (no silent loss);
new input remains constrained by real Wayland focus.

Suggested bounds:32 args,32 environment entries,1024 bytes per string,32768
combined string bytes. The frontend validates owned strings/arrays; this is a
private FFI, not an untrusted pointer RPC. A final header and Rust layout probes
must freeze exact fields/offsets before implementation. Every caller must adopt
the checked adapter or remain unreachable under standalone policy. It is not
a complete process sandbox or a solution for arbitrary background jobs.
