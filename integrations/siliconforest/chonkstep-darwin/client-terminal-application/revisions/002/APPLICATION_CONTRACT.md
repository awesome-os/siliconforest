# Terminal application contract — agreed 001

This separately named application derives from frozen
`client-terminal-native-startup/source-validation.json` source set
`ae9bd9759cd6f2ada2fda7bde5759bfce13f7f3e10f0fa365c7f9b53b5640fff`.
The original observer, its receipts and its qualified runtime remain unchanged.
Root approved this API and isolated implementation on November 8, 2026. This
contract is not runtime evidence.

## Launch boundary

Binary: `chonk-terminal-darwin`. Exact argument order:

```
chonk-terminal-darwin --socket /absolute/socket --instance HEX48 --records /absolute/private-directory --seconds N
```

`HEX48` contains exactly 48 lowercase hexadecimal bytes, unique per launch.
The session creates and owns the instance directory with mode 0700; the app
verifies its type, owner and permissions without following a final symlink.
It creates its record files exclusively, mode 0600. Existing app filenames
refuse launch. Session stdout/stderr files may already exist in this directory.
The explicit socket and record paths are bounded and absolute. `N` is a finite
remaining application lifetime, initially 1–120 seconds, selected inside the
session's outer deadline with its shutdown budget reserved. No ambient socket,
PATH executable search, challenge, expected output or automatic restart exists.
Title: `Terminal <first 12 instance hex bytes>`; app ID: `chonk-terminal-darwin`.
Those display strings authorize neither process ownership nor signalling.

The session's reviewed pinned-SDK C adapter supplies stdin/stdout/stderr and a
private connected Unix stream at fd 3 through explicit `adddup2` actions under
`POSIX_SPAWN_CLOEXEC_DEFAULT`. The terminal adopts only fd 3 as its status channel,
verifies stream type, sets CLOEXEC/nonblocking, and uses no spawn API itself
except the existing owned PTY fork/exec implementation. The PTY is created before
opening Wayland. Its child closes every fd >=3 except its promoted exec-status
pipe; the latter closes on successful exec. In particular fd 3, Wayland, record
and diagnostics descriptors do not survive in `/bin/sh` (DS4).

## Status protocol

One-way ASCII newline-delimited frames on fd 3, maximum 256 bytes including LF:

```
CHONKTERM/1 READY HEX48 APP_PID\n
CHONKTERM/1 CLEANUP HEX48 APP_PID COMPLETE RAW_WAIT_STATUS TERM_SENT KILL_SENT\n
```

Decimal integers have no sign except RAW_WAIT_STATUS, a signed 32-bit integer
which may be `-1` when no terminal status was obtained. COMPLETE=1 requires an
actual raw wait status and forbids the `-1` sentinel. COMPLETE/TERM_SENT/KILL_SENT
are exactly 0 or 1. Session parsing validates these bounds and exact fields.
The app sends READY at most once and CLEANUP at most once, in that order if both
exist. CLEANUP may occur without READY. EOF, duplicates, oversize/truncated lines,
unexpected identity/PID or invalid fields are never cleanup acknowledgement.
The session validates against its own child and instance, never a Wayland title.

Messages use bounded partial-write state with EINTR/EAGAIN handling and a short
finite final flush. A broken/full channel causes a recorded protocol failure
and owned cleanup; it must never block the event loop or skip cleanup. CLEANUP
is emitted only after actual PTY cleanup and synced final records. COMPLETE=0
or missing CLEANUP remains incomplete even if the session reaps the app.
The channel alone does not correlate an unrelated Wayland surface with the child.

READY reports checked PTY setup/exec, parsed XKB map, SSD configure/ack and an
actual committed frame callback. It does **not** require focus, empty held keys,
neutral modifiers, a shell response or physical input. The app may start while
another client or a modal has focus. Final records retain each actual gate.

## Ordinary input and lifetime

Wayland enter records held keys without translating them. Thus Return/Super held
through launch/map produce no CR, including when their later releases arrive.
Modifiers follow the compositor-supplied XKB masks. A new focused press alone
can produce text; releases never generate bytes. Normal enter/leave and releases
for keys owned before a focus change are tolerated without fake events. Invalid
protocol state remains a failure; compositor global HID sequence validation is
separate from this application's focus-local key bookkeeping.

**DS1 choice:** finish already accepted PTY writes after focus leave. Leave stops
accepting new focused text but does not clear the bounded queue or reject the
app. Short writes and EAGAIN preserve exact byte order; written-byte transcripts
record only actual successful writes. Closing/EOF/error may retire remaining
bytes only with an explicit final unsent-byte count and stop cause, never silent
loss. No synthetic key presses/releases repair focus transitions.

Retain real XKB translation, TERM=dumb grid, SHM buffer ownership/release, SSD
negotiation, resize and idempotent child cleanup. The shell is `/bin/sh -i +m`,
TERM=dumb. No full xterm/editor compatibility, detached/background jobs or changed
job-control support is claimed. Keep the existing checked foreground-group
refusal. Transcript and input queues remain bounded; limits cause recorded
failure and cleanup rather than unbounded growth.

Window close, legitimate shell EOF, TERM/INT/HUP and the finite deadline are
normal application stop causes after complete owned cleanup. They do not require
READY or shell exit 0; record actual wait status. Protocol, exec/setup, quota,
status-channel and cleanup failures remain explicit nonzero outcomes. A failed
record write still triggers cleanup; success requires its final records.

Private files include actual PTY RX/TX, checked child readiness, final grid and
`result.json` with instance/PIDs, readiness gates, stop cause, accepted/written/
unsent input counts, unsupported terminal sequences and real cleanup outcome.
No startup-only qualification labels, fixed command recognition or physical-input
claim remains. Bytes displayed by the renderer come only from PTY output.

## Required host verification before root's build admission

Preserve and run real host PTY setup/exec failure, session/ctty, foreground refusal,
TERM/KILL/reap and Wayland disconnect tests. Add bounded fd 3 protocol tests and
a real exec-descriptor fixture proving the shell cannot access the status/log
sentinels while its checked exec pipe works. Exercise two distinct directories,
create-new collisions, private permissions, normal EOF/close before READY and
missing/full/broken status channels. Input fixtures must cover DS1 accepted text
then immediate leave, partial write then leave, focus changes with held modifier
and release, unfocused readiness, and DS5 Super+Return held through map/released
without CR. These use synthetic host input only; full launch-chord ownership and
two-client integration also require the session's real Wayland/Shell tests.

No target execution, hardware access, native input qualification, latency,
multitouch, GPU or Wi-Fi result follows from this contract or host tests.
