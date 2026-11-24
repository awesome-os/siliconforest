# Finite Darwin Wayland PTY terminal

This separate client runs the guest's real `/bin/sh` on a checked PTY and renders
only its output. It preserves the original, SSD and input clients and the
compositor. The source is ready for independent review and root's native build;
Linux host tests do not qualify Darwin PTYs or the M1 display. A successful
reviewed Darwin PTY smoke is a prerequisite to any UI hardware qualification.

```
darwin-wayland-terminal --socket /private/var/run/PRIVATE/wayland --nonce HEX48 --seconds 120
```

The socket path must be absolute, its immediate parent an owned private directory
(no group/other permissions), and the nonce exactly 48 hexadecimal characters.
Duration is 1..120 seconds. The production CLI refuses non-ARM64-Darwin targets.
This client does not create a server, open HID devices, prime queues, or inject
keys. Root owns the separate guest `DL11_TERMINAL_PRIME_<nonce>` marker and fixed
host-origin typing helper. The compositor must already expose a real keyboard
seat; pointer capability is not needed by this client.

## Exact evidence interface

One line is flushed after **all** prerequisites:

```
DL11_TERMINAL_READY <nonce> origin=host_sfserialhid pty=ready keymap=xkb_v1 focus=own_surface frame=ready
```

Those prerequisites are checked child session/controlling terminal/stdio,
successful `/bin/sh` exec boundary, actual PTY output, a parsed compositor-supplied
XKB v1 map, own-surface keyboard focus with no held keys and neutral modifiers,
acknowledged server-side decoration, and a completed frame callback for a buffer
that includes observed PTY output. An initial blank frame cannot authorize READY.
A frame callback is not independent proof of panel scanout. Host-origin is the
qualification label, not device provenance inferred from a Wayland event.

The helper may then type its fixed `printf` command joining the two nonce halves,
followed by `uname -s`, `uname -m`, `uname -r` and Enter. `/bin` is first in the
child PATH. There is **no** production write of that command into the PTY: the
sole production input source is translated Wayland keyboard events. No key
repeat synthesis, clipboard, socket command channel, or shell evaluator exists.

The following marker is flushed once only if the post-READY PTY byte stream,
with **CR bytes alone removed**, contains the exact contiguous sequence
`\nDLT_<nonce>\nDarwin\narm64\n25.5.0\n`:

```
DL11_TERMINAL_RESPONSE <nonce> origin=pty_shell system=Darwin machine=arm64 release=25.5.0
```

Warnings, ANSI escapes, NUL, extra blank lines and different releases are not
filtered. A command echo or response received before READY cannot satisfy this
check. The recognizer never supplies pixels or PTY input. Rendering consumes the
unmodified PTY bytes through a separate text-grid module. Future qualification
must also bind the raw host TX ledger, exact artifacts, guest logs, frame and
cleanup/recovery; this self-reported marker alone is not authenticated proof.

Private records are created with exclusive creation and mode 0600 in a new 0700
`<socket-parent>/terminal-<nonce>/` directory (existing directories are refused):

- `pty-rx.bin`: every consumed PTY read, without filtering, including a quota-crossing final chunk.
- `pty-tx.bin`: only bytes actually accepted by PTY write, respecting partial writes.
- `child.json`: parent and child IDs recorded immediately after fork, before Wayland.
- `child-ready.json`: the child-checked SID/PGID/controlling-TTY and exec-pipe boundary.
- `grid-final.txt`: actual final grid on normal deadline/close/PTY EOF.
- `result.json`: strict observation status or error, plus raw child wait status and explicit cleanup result.

Root must collect those private files before guest teardown. No key/PTY payload is
printed to the serial log by the client. `DL11_TERMINAL_DONE ... cleanup=reaped`
and exit 0 require both the strict response and completed child cleanup. On
protocol errors raw files still remain and cleanup runs; a final grid/session
summary may be absent. A write/storage failure is an error, never evidence of a
complete transcript.

RX/TX use unbuffered `File::write_all` per consumed read/accepted write prefix;
there is no userspace writer buffer to flush before a live read. RX is additionally
synced before RESPONSE and both files are synced at shutdown. The client does not
fsync each keystroke. A separately bound live collector can read the files while
the terminal renders; that is a concurrent prefix snapshot, not final exit or
cleanup evidence. `result.json` and `grid-final.txt` normally do not exist yet.

## PTY and cleanup contract

The ABI is `libc = 0.2.190`'s Apple ARM64 definitions, plus an explicit declaration
of pinned SiliconForest's exported `ptsname_r(int,char*,size_t)`. No `forkpty`,
`login_tty`, or unchecked `openpty` setup is used. `pty.rs` checks
`posix_openpt/grantpt/unlockpt/ptsname_r`, opens the slave `O_NOCTTY`, verifies
`tcgetattr/tcsetattr` and `TIOCSWINSZ`, and makes the master nonblocking.
The initial slave is canonical, echoing, ISIG-enabled, ICRNL/ONLCR, 80x24.
Interactive shells may subsequently change their own termios; the terminal
honors that rather than resetting it while the shell runs.

The child performs checked `setsid`, `TIOCSCTTY`, `dup2(0,1,2)`, CLOEXEC removal
on stdio, `isatty`, SID/PGID/foreground-group verification, signal reset and
`execve`. All argument/environment memory exists before fork. Post-fork code
uses raw libc calls and `_exit`, without Rust allocation/destruction or stdio.
Internal descriptors are CLOEXEC and above 2. Other descriptors are closed up
to the checked `_SC_OPEN_MAX` bound (maximum 1,048,576); the controlled launcher
must not hand in descriptors above a previously lowered descriptor limit.
The parent never retains the slave. A CLOEXEC status pipe records setup before
exec; failure after setup is still failure, and partial/malformed status is
refused. Setup/exec has a 5-second observation deadline.

The executable is exactly `/bin/sh`, argv `sh -i +m`, with `TERM=dumb`,
`LC_ALL=C`, `ENV=/dev/null`, controlled HOME/PATH/PS1/PS2/ZDOTDIR and
`HISTFILE=/dev/null`. On the current image the real sh selector execs zsh in sh
emulation. `+m` requests no job-control process groups. The qualification command
must stay finite and must not spawn background/detached jobs, change session or
group, or replace the test's cleanup contract. Before each input write the actual
PTY foreground group must still equal the owned shell group. This is not a
sandbox or general descendant supervisor for arbitrary shell programs.

TERM/INT/HUP handlers only set an atomic flag. Deadline, window close, disconnect,
normal PTY EOF and error paths converge on idempotent cleanup: signal the owned
child/session with TERM, allow 250 ms, then KILL, then bounded `waitpid(WNOHANG)`
for up to two seconds. The child remains unreaped until the final signalling is
over, preventing PID-reuse signalling. Session identity is rechecked at KILL to
cover a child still doing setsid during TERM grace. No kill(0), kill(-1), reset,
or assumed master-close SIGHUP is used. Failures/timeouts remain incomplete.

SIGKILL/process crashes cannot run cleanup. Root's outer guest supervisor must
handle that case and verify owned processes are stopped. The small fork-to-first
record window and arbitrary detached descendants are not solved by this app.
Do not treat a missing result file as cleanup success. All timing bounds are
userspace observation bounds; they cannot preempt a stalled kernel syscall.

## Wayland, font and grid

Registry synchronization uses a normal asynchronous `wl_display.sync`, never an
unbounded roundtrip. Socket connection has a 3-second poll deadline; readiness
has a 20-second deadline. Every turn services bounded PTY reads and nonblocking
partial input writes, then polls Wayland and PTY for at most 20 ms. Prepared reads
are cancelled when unreadable, and socket hangup/error terminates the session.

SSD must actually configure ServerSide followed by an acknowledged xdg-surface
configure. The single SHM buffer uses a real regular unlinked O_RDWR file and
fd passing, XRGB8888, without mmap page-size assumptions. Neither repaint nor
resize reuses a compositor-owned buffer; both wait for release **and** the frame
callback. Resizing also updates the PTY winsize. Decoration destruction precedes
toplevel/surface destruction. The compositor retains any still-referenced SHM
fd when the client disconnects; no shared storage is reused for other content.

The monochrome grid is bounded to 20..160 columns and 6..60 rows (initial 80x24).
It preserves ASCII case and punctuation, CR/LF, wrapping/scrolling, backspace,
tabs, basic bounded CSI cursor/erase operations and reset-only SGR. Other modes
are counted, not advertised. OSC is discarded through its terminator without
clipboard/title action. Non-ASCII has a visible replacement glyph; there is no
claim of Unicode width, alternate screen, full xterm, composition, or terminal
application support. The total RX bound is 1 MiB, TX bound 16 KiB; no scrollback.

Actual xkbcommon parses the received map and translates evdev key+8 under the
received modifiers/group. Return, Backspace, Tab and Escape are mapped explicitly;
other text comes from `key_get_utf8`. Releases never insert text. The IBM Plex
Mono font is copied unchanged from the pinned local Chonkstep tree, with its
SIL OFL license and exact upstream/local provenance in `fonts/`. `ttf-parser`
outlines and tiny-skia rasterization provide real case-correct glyphs.

## Offline validation and remaining gates

```
cargo test --offline --locked
cargo clippy --offline --locked --all-targets -- -D warnings
cargo fmt --check
```

Host tests cover actual host PTY execution/roundtrip, initial termios, resize,
setup/exec failures, silent-child refusal, forced termination, idempotent reap,
Wayland disconnect via anonymous socketpair, real XKB translation, grid controls,
font glyphs, SSD ordering, buffer ownership and strict response matching. They
do not execute the guest libc, a live compositor, or hardware.

All Cargo dependencies were already cached. Cargo.lock pins exact transitive
versions, including existing extracted cfg-if 1.0.4 and syn 3.0.3; nothing was
fetched or installed. Root owns the native cross-link with the existing SiliconForest
SDK wrapper and libxkbcommon archive, source/binary review, PTY smoke prerequisite,
launcher, fresh typing helper, runtime capture, and Linux recovery. This source
freeze is not a native build or terminal runtime claim.
