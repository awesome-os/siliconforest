# chonk-terminal-darwin

Separate ordinary terminal application derived from the frozen native-startup
observer, whose 14 original source hashes remain intact. The app uses a real
owned `/bin/sh -i +m` PTY, compositor XKB keymap, an xterm emulation (TERM=xterm-256color, see below), CPU font
renderer, SHM buffer ownership and server-side decorations. The former response
recognizer and startup-only title/readiness policy are removed.

The agreed launch/status interface is in [APPLICATION_CONTRACT.md](APPLICATION_CONTRACT.md).
The real desktop session supplies absolute socket/record paths, a unique lowercase
48-hex instance ID, finite remaining lifetime and private Unix stream fd 3.
The terminal forks its owned PTY child before opening Wayland; that child closes
fd 3 and every other descriptor above stdio except its CLOEXEC exec-status pipe.
Root owns the separate pinned-SDK C launch adapter; this crate does not implement
or guess Darwin spawn attribute layouts.

Readiness requires checked shell setup/exec, XKB map, SSD negotiation and an actual
frame callback; it is independent of focus or held keys. Enter's held-key array
never becomes typed input. Accepted PTY writes remain queued in order after
focus leave. Normal close, shell EOF, signal and deadline stop cleanly after owned
cleanup even before READY. Final records retain unsent input counts and real raw
wait status. A status-channel/final-record failure is an application failure.

Limits are explicit: the emulation is what the section below lists, no detached
or background jobs, and no foreground-group relaxation. Input is bounded to
16,384 lifetime bytes and PTY output to the inherited 1 MiB budget (one bounded
read may expose quota overflow before shutdown). The supplied 1–120 second
lifetime bounds application operation; its existing 250 ms TERM grace/2 second
reap budget and up to 250 ms status flush must fit the session's reserved shutdown
time. An OS syscall that stops returning still requires root's outer supervisor.
The inherited shell environment remains explicit and fixed; GUI environment or
launch expansion requires coordination with the session contract.

Host checks (Linux only, synthetic input, no target execution):

```
cargo test --manifest-path Cargo.toml --locked --offline --target-dir target-host
cargo build --manifest-path Cargo.toml --locked --offline --target-dir target-host
```

Tests need host xkeyboard-config/libxkbcommon and Python 3 for the real foreground
group fixture. Test-only wayland-server 0.31.14 drives an actual anonymous local
Wayland client connection, XKB fd, SSD configure/ack, SHM fd, buffer release and
frame callback. Its presentation acknowledgement is a wire fixture, not a renderer
or physical scanout witness. Cases cover unfocused startup, held launch keys,
typed bytes followed by Leave, close/EOF before READY, disconnect cleanup,
real shell descriptor exclusion, setup/exec failures, resize and TERM/KILL/reap.
Partial writes, full/broken status channels and private-record collisions have
separate bounded tests. The session must still qualify complete launch-chord
ownership, two-window interaction and actual target execution.

Host tests do not establish Darwin execution or physical input. Native keyboard
and pointer observations belong to the preserved earlier receipts; raw HID wire
correlation remains separate. Root owns SDK build, artifact binding and hardware.

## Long sessions (10 November)

`src/` here is now the source the deployed binary is built from: the fd-boundary revision that
`runs/m1-desktop-010/terminal-application-build-002` was built from (its `FD_BOUNDARY_CONTRACT.md`,
`fd-boundary.patch` and `test_fd_mutations.py` are copied alongside), plus the change in
`long-session.diff`, made for interactive desktop sessions of up to four hours:

- `--seconds` accepts 1..14400 (was 1..120);
- `MAX_RX` (PTY output, which is also written to the records directory in the RAM root) is
  8 MiB per terminal (was 1 MiB); `MAX_TX` (typed input) is 1 MiB (was 16 KiB).

A terminal still ends itself when it passes either quota. The binary built from this source is
`runs/m1-desktop-010/terminal-application-build-003/target/aarch64-apple-darwin/release/chonk-terminal-darwin`
(same imports and size as build-002; 43 host tests pass):

    CARGO_TARGET_AARCH64_APPLE_DARWIN_LINKER=$PWD/../darwin-linker.py \
      cargo build --offline --locked --release --target aarch64-apple-darwin --target-dir <run>/target

`source-validation.json`, `host-tests.log` and `revisions/` describe earlier revisions and were
not regenerated for this one.

## Terminal emulation and scrollback (10 November, build-004)

The TERM=dumb grid (`src/grid.rs`) is gone. `src/term.rs` wraps the `vt100` crate
(0.18.1, parser `vte` 0.15.0; with `arrayvec` 0.7.8, `itoa` 1.0.18, `memchr` 2.8.3,
`unicode-width` 0.2.3 — all pinned in `Cargo.lock`, built `--offline --locked` from
the cargo registry cache like the other dependencies; no platform code in any of them).

* Screen: cursor addressing, erase, insert/delete, scroll regions, SGR with the 16,
  256 and 24-bit colours, bold, dim, underline, inverse, the alternate screen (1049/47),
  cursor hide/show, wide characters. `CSI s`/`CSI u` are mapped to save/restore
  cursor (toybox `vi` measures the screen with them). The terminal answers `CSI 5n`,
  `CSI 6n` (cursor position), `CSI c` and `CSI > c`.
* Environment: `TERM=xterm-256color`, `COLORTERM=truecolor`. The RAM root has no
  terminfo; the boot wrappers stage `../terminfo/xterm-256color` (ncurses 6.6, sha256
  1eb6ddea…) as `/usr/share/terminfo/x/xterm-256color` and `/usr/share/terminfo/78/…`.
* Keys (`src/keys.rs`): arrows (CSI, or SS3 while the program has application cursor
  keys on, `CSI 1;m X` with modifiers), Home/End, Insert/Delete/PageUp/PageDown,
  F1–F12, Shift+Tab, Alt as an ESC prefix.
* Resize: a configure changes the grid; the PTY gets TIOCSWINSZ and the foreground
  program SIGWINCH. Bounds: 20–400 columns, 6–150 rows.
* Scrollback: 5000 lines. Two-finger scroll (wheel events, three lines a detent) and
  Shift+PageUp / Shift+PageDown (a page less one line) move the view; typing returns
  to the bottom. On the alternate screen wheel steps are sent to the program as
  Up/Down arrow keys instead.
* Drawing: only rows whose content changed are redrawn, written to the shm pool (since
  build-007 shared memory, see the last section) and damaged.
* State for checks from outside: `<records>/term-live.txt`, rewritten (rename) at most
  every 200 ms when it changed: a `TERMSTATE cols= rows= alternate_screen= application_cursor=
  bracketed_paste= cursor= cursor_hidden= scrollback_offset= scrollback_lines= unsupported=`
  line, then the text on screen. The compositor's autopilot reads it.

Open: no mouse reporting to programs. Blink and italic are parsed but not drawn differently.

Host tests (`cargo test --offline --locked`, 51): emulator unit tests, and
`host_full_screen_programs_draw_and_exit_cleanly`, which runs the host's real `less`
and `vim` on a PTY through the emulator.

## Selection and clipboard (10 November, build-005)

* Select: press the left button on a character and drag; the text from the press to the
  pointer is highlighted (colours exchanged), whole lines in between. The text is taken
  when the button is let go, so it stays copyable after new output has moved the
  highlight away (output, scrolling and resizing remove the highlight). A click without
  a drag removes the highlight.
* Copy: Control+Shift+C, Command+C or Control+Insert offers the selected text as the
  seat's selection (`wl_data_device.set_selection`; types `text/plain;charset=utf-8`,
  `UTF8_STRING`, `text/plain`, `TEXT`, `STRING`). Control+C alone is still the interrupt.
* Paste: Control+Shift+V, Command+V or Shift+Insert reads the current selection through a
  pipe (non-blocking, at most 256 KiB, two seconds) and types it. Control characters other
  than tab and line ends are dropped; line ends become carriage returns; when the program
  has bracketed paste on (DECSET 2004) the text goes between `ESC [ 200 ~` and
  `ESC [ 201 ~`. A paste that does not fit the remaining input quota is not typed.
* These are the bindings of Linux terminals; Control+Insert / Shift+Insert are what
  Chonkstep's "universal copy/paste" (Super+C / Super+V) sends on Linux.
* `wl_data_device_manager` is optional: without it (an older compositor build) the
  terminal still selects but neither copies nor pastes; `term-live.txt` says
  `clipboard=absent`. Its state line also carries `selected_bytes= copies= pastes=
  pasted_bytes=`.
* `src/clipboard.rs` holds the type choice, the paste filter and the two pipe pumps.

## Output scale (11 November, build-006)

The terminal draws at the output's real pixel density. `src/scale.rs` holds every rule, in
integers; a scale is a whole number of 120ths.

* Scale S, first that applies: `wp_fractional_scale_v1.preferred_scale` (only when
  `wp_viewporter` is there too, so it can be presented), `wl_surface.preferred_buffer_scale`
  (`wl_compositor` 6), the largest `wl_output.scale` of the outputs the surface entered, 1.
  All of them are optional: a compositor with none gets exactly what build-005 drew.
* Font: 18 logical px, or `CHONK_TERMINAL_FONT_PX` (whole number 6..96, logical px) in the
  terminal's own environment. Glyph outlines are rasterised at font px x S; nothing is drawn
  small and enlarged. Cell, with f = font px x S: width round(0.6 f), height round(22 f / 18),
  baseline round(17 f / 18), padding round(8 S) on each side (11 x 22, 17 and 8 at scale 1).
* A configure of W x H logical: columns = floor((round(W S) - 2 pad) / cell width), rows
  likewise, bounded to 20..400 x 6..150; grid = cells + 2 pad device px. Committed: logical
  L = ceil(grid / S) per axis (never more than the configured size), buffer B = round(L S),
  halves up; the at most ceil(S) spare pixels right and below are background. With the
  fractional protocol: buffer scale 1 and `wp_viewport.set_destination(L)`. Without: whole
  scale N, buffer N x L, `set_buffer_scale(N)`. Window geometry and min/max size are logical;
  damage is `damage_buffer`.
* A new scale re-rasterises the glyphs, recomputes the grid for the last configured logical
  size (or keeps the columns and rows if the compositor never gave a size) and sets the PTY
  size (TIOCSWINSZ), without waiting for a new configure.
* Pointer positions (logical) are multiplied by S before they are mapped to cells. Wheel
  travel is logical: three lines a detent at every scale.
* Each change prints one line on stderr:
  `TERMSCALE scale120=240 font_px=36 cell=22x44 grid=80x24 buffer=1792x1088 logical=896x544 pad=16 buffer_scale=1`.
  The `TERMSTATE` line of `term-live.txt` ends with `scale120= cell= buffer= logical=`, and
  `result.json` has `session.scale`.
* A configure outside the bounded grid is still refused; against a compositor that scales, an
  eighth either way is tolerated and bounded instead (cell rounding differs between scales).
  The 64 MiB buffer bound is multiplied by ceil(S) squared: the same window area as before.
* `wayland-protocols` now also has its `staging` feature on (for `wp_fractional_scale_v1`);
  `Cargo.lock` is unchanged.

Host tests (`cargo test --offline --locked`, 65): the rules above for the steps 120, 150, 180,
210, 240, 300 and 360; the fixture compositor sending fractional 240 then 180 (and 150 then
300 with a configured size, whole scales through `preferred_buffer_scale` and `wl_output`,
and no scale protocol at all), checking buffer size, viewport destination, buffer scale,
window geometry, PTY size, a drag selection and the wheel; and
`text_is_rasterised_at_device_pixels_at_every_scale_not_enlarged`, which with
`CHONK_SCALE_PROOF_DIR=<dir>` also writes the rendered lines (PPM) and its numbers there.
Not run on the M1 yet.

## SHM buffers in memory (11 November, build-007)

Each wl_shm pool used to be an unlinked file in the records directory, which is on the RAM
root: 7.4 MiB for an 80 x 24 terminal at 200%, 16 to 18 MiB at 300%. `src/shm.rs` now owns
the pool's memory:

* First POSIX shared memory: `shm_open("/cht-<pid>-<n>", O_RDWR|O_CREAT|O_EXCL, 0600)` (n
  counts the objects of the process; at most 26 bytes, Darwin allows 31), `shm_unlink` at
  once, close-on-exec checked (and set if the platform had not), one `ftruncate`, one
  `mmap(PROT_READ|PROT_WRITE, MAP_SHARED)`. Changed rows are copied into the mapping. On drop
  the wl_buffer and the pool are destroyed, then the mapping and the descriptor go.
* If any of those steps fails, what it made is released and the earlier unlinked file in the
  records directory is used (seek + write, as before). There is no retry. One line on stderr
  says why: `TERMSHM shm buffer 896x544 (1949696 bytes): posix ftruncate: errno 28; using a
  file in the records directory`. `CHONK_TERMINAL_SHM=file` in the terminal's own environment
  asks for the file directly (any other value: shared memory first). It is not passed to the
  shell.
* A failure of both, or of a later write, ends the terminal with the buffer, the backing, the
  operation and the errno, e.g. `shm buffer 2610x1632 (17038080 bytes): file set_len: errno 28
  (after posix ftruncate: errno 28)` or `...: file write 208800 bytes at 0: errno 28`. That
  text is in `result.json` `errors` (with `session.stop_cause` `fault`) and on stderr.
* Where to see the backing: the `TERMSCALE` line and the `TERMSTATE` line end with
  `shm=posix` or `shm=file`; `result.json` has `session.shm` = `{"backing", "fallback" (why
  shared memory was not used, or null), "posix_buffers", "file_buffers"}`.
* Limits are unchanged (64 MiB x ceil(scale) squared per buffer, one buffer at a time, damage,
  release handling).

Host tests (`cargo test --offline --locked`, 75; run them with `TMPDIR` and `XDG_STATE_HOME`
set to a scratch directory): `shm::tests::*` (name, unlink, close-on-exec, zeroed, a second
mapping sees the rows, every failing step falls back, the error texts), three
`host_real_wayland_*` cases in which the fixture compositor maps the pool descriptor it was
sent and compares it with the rendered screen and with the file backing, and
`host_shell_environment_is_the_fixed_list_not_the_terminals`. Not run on the M1 yet: that
Darwin's `shm_open` objects behave as the kernel source reads is not established by these.
