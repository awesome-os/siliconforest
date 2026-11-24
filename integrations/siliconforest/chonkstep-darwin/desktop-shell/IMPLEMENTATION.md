# Isolated Shell bridge — source checkpoint 001

This checkpoint implements the real shared `chonk_shell::Shell` and cascade
over the CPU backend. It is host-tested source for independent review, **not a
native session candidate or runtime admission**. The earlier DESIGN004 and
its archived revisions remain exact design records.

`shell.patch` applies to the saved deployed717 source at
`runs/m1-desktop-010/keyboard-only-compositor-build-001/source`. The local
`source/` copy is working material, ignored by Git. `source-validation.json`
binds every changed file before/after, the patch, tests and dependency inputs.
The qualified source tree, native frontend entry, HID decoder, presenter,
terminal application, root spawn implementation and all integration-specific
modules stay unchanged. The generic Shell constructor still selects its full
profile; only the explicit standalone constructor omits its external services.

Implemented paths:

* Real Shell/Desktop/cascade, CPU PopupHost, shell-surface hit order and modal
  grabs; actual wm-core window policy/decorations remain in use. A real two-client
  Wayland fixture exercises a held launch chord while the shared menu has focus.
* Standalone fixed theme/config and a bounded managed Terminal request bridge.
  Generic app discovery, control listener, autostart, config reload, external
  appearance publishing and external integration entry points are absent from
  this profile. Unsupported direct actions are refused as well as hidden.
* Owned positive child PIDs, strict fragmented CHONKTERM/1 parsing, independent
  application reap and PTY-cleanup acknowledgement, bounded shutdown and sticky
  failure observations. `child_created=1` retains the PID even on C cleanup or
  output-validation failure; only its direct owner can wait/signal it.
* Exact frozen application CLI, unique private0700 instance directories,
  exclusive0600 stdout/stderr, selected stdin and connected status endpoints.
  No PATH executable lookup or inherited environment. Sources remain borrowed
  through C; the parent's copy of the child endpoint closes after the call.

## Explicit native linkage boundary

Default `darwin-backend`/`desktop-shell` builds have an Unsupported native
spawner. `native-managed-spawn` additionally compiles an Apple-only declaration
of the real `chonk_darwin_spawn_v1`; root must explicitly link the exact frozen
C object. No C source is copied here and no alternative shim exists. Host builds
of that feature still refuse native spawn. The56-byte request/result layout and
offsets have compile-time assertions. The expected frozen policy is exactly
0x400e (CLOEXEC_DEFAULT, SETSIGDEF, SETSIGMASK, SETPGROUP), with matching readback.

Root's C source set is c1a3bf825ad9569817f2966aa4d8ec3f382c03c1cf23f80fb68c5472c724d7db;
the SDK object is71c322c1f47bb16373e01f2a3ff7b02690b441faf9a582327304543316a6c729.
The ordinary application source set is216de1cd301f520f7db8b33f507c047b548da728a554f4a7ba2af4e9b343fbf5.
These are consumer bindings, not independent C review or native execution proof.

`Server::enable_desktop_shell` and `configure_managed_applications` are explicit
library seams. No executable/records/socket is selected by environment. The
existing native `main.rs` is unchanged and cannot enter this new session.

## Launch and readiness diagnostics

The manager keeps128 attempts, including refusals that create no child. Each
retains the Shell request, private instance, first gate times, actual PID,
complete C result, first failure, status-byte count and terminal wait result.
Observed READY is never erased by later malformed input or failure. It is an
application assertion, distinct from the manager's overall outcome.

Gates are admission, status-nonblocking, C-return, PID-owned, application READY,
CLEANUP, stream EOF, application reap, TERM attempt and KILL attempt. A missing
gate stays unknown; EOF alone is not cleanup. The frozen protocol does **not**
expose separate PTY/keymap/SSD/frame progress before READY. Those remain unknown
in manager diagnostics and must be obtained from the application's actual final
`result.json`/private logs. No configure-serial or map-order inference is made.

Before an actual configured launch, the manager adopts an exclusively created
regular0600, current-UID, single-link empty journal FD, sets CLOEXEC and writes
bounded append records. Gate records are synced; failure is sticky and blocks
subsequent launches. Existing ownership cleanup continues on journal failure.
The journal cap is2MiB; snapshots are bounded by128 attempts. No environment,
PTY/key bytes or arbitrary client-controlled text enters it. The caller retains
private records; nothing is sent to an external service.

## Time and exit contract

The application owns its PTY and starts its own20-second READY bound. The manager
allows25 seconds for READY, records a timeout, then signals only its direct
wait-owned app. End Session, app/status faults, an initial native cleanup error
and the finite app lifetime also begin the same shutdown. TERM is attempted
once; after5 seconds a still-owned running child receives one KILL; after another
3 seconds the reap result is incomplete. A reaped app gets at most2 seconds for
status EOF. ECHILD/unknown ownership stops all signals. Later reaps are retained
without rewriting an already failed deadline as success. COMPLETE cleanup can
legitimately occur before READY; that does not establish app readiness.

Configured sessions are11–180 seconds; per-app CLI lifetimes are1–120 seconds,
capped by remaining session time with10 seconds reserved. This is a source
policy, not measured target scheduling or a guarantee against blocked filesystem
I/O. The external controller's hard deadline remains a separate root concern.

## Remaining admission work

1. Independent review of this source/profile and the exact C leaf; real Darwin
   FD-whitelist fixture. Source flags and host facades do not prove exec closure.
2. Bind root's separately reviewed IPC portability input before a native Shell
   build. This copy deliberately still contains the unmodified pinned IPC crate.
3. Add/review the explicit finite native session entry. It must drive manager
   cleanup on **every** frontend error/signal/End Session before destroying the
   display/input/SFGOP owners. The library has no blocking Drop reaper and must
   not be admitted as a complete session without that outer cleanup owner.
4. Bind real application binary, fonts, runtime directories and package closure;
   independently review the standalone side-effect call graph. Source/host tests
   do not establish full policy coverage or compatibility of the generic profile.
5. Root's later reviewed target workflow, per-app gates, actual application
   records and verified Linux recovery. No hardware was used for this checkpoint.

The last native two-window failure and its18/30 result are unchanged. Nothing
here diagnoses TW1, qualifies physical input, or addresses performance/GPU/Wi-Fi.
