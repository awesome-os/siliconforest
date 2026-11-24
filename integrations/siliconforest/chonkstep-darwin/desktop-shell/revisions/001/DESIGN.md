# Darwin CPU desktop shell — design 001

Status: **design for review, not implemented, compiled, or booted**.

## Baseline and intended result

Build on the actual deployed `717da106a653d72f0d75aa2407fdd5da521a2dbc919502ef563c0927b6f72b57`
compositor and its saved source at
`runs/m1-desktop-010/keyboard-only-compositor-build-001/source`.
That source is pinned Chonkstep `e0fe3a000aec71b1db46b940e2a75670be4e0f31`,
the frozen Darwin backend, and the reviewed keyboard-only capability patch.
The deployed passive-child session used its **Both** input mode. The manifest
binds the inspected source files, build record, binary, and terminal source.
Older documentation saying that this backend has never run is historical;
the actual runtime receipts remain separate evidence.

The next functional increment is a real Chonkstep desktop that launches its
native terminal from a Chonkstep menu or key binding. Two independent terminal
windows can receive native input, change focus, move/resize, close, and reopen.
Each owns a real Darwin PTY and `/bin/sh`; content comes only from that PTY.
End Session closes owned applications and releases the compositor's resources.
The existing `chonk_shell::Shell<CpuBackend>` must own desktop/menu policy and
use the actual `Desktop`, cascade, theme, and wm-core implementations. Do not
substitute a test menu, painted dock, alternate window policy, or fake launcher.

This is a bounded desktop workflow increment, not general application
compatibility. Keep TERM=dumb, one output, the existing CPU/PDGOP presenter and
native keyboard/pointer. No performance work, raw HID tracing, multitouch,
GPU, Wi-Fi, kernel, firmware, packet, host-controller, or hardware changes are
part of this design. T2's identity/state qualification and the human physical
use witness remain intact; exact raw HID-byte correlation remains open.

## Source findings that determine the integration

Paths in this section are relative to the saved compositor source unless
otherwise stated. These are source observations, not new runtime findings.

| Evidence | Consequence |
| --- | --- |
| `crates/wm-darwin/src/server.rs:30-84`, `Cargo.toml:8-19` | Server owns a `WindowManager<CpuBackend>` and protocol state; it has no `chonk-shell` dependency or `Shell` instance. |
| `scene.rs:128-151,439-500` | CPU shell surfaces already have owned pixels, geometry and stacking. Hit-testing covers client/frame only. Shell surfaces are currently not interactive. |
| `wm-theme-api/src/popup.rs:23-42`; `wm-theme/src/cascade.rs:157-172` | `Shell` needs a genuine `PopupHost`. A successfully opened cascade acquires pointer and keyboard ownership, and close releases both. |
| `wm-core/src/backend.rs:282-291,583-584` | Default shell event drains and keyboard grabs are inert; Darwin must implement them before enabling menu/switcher input. |
| `chonk-shell/src/shell.rs:1240-1397,3204-3379` | Generic construction/ticking also scans apps, opens a control socket, watches configuration, restores/stores layouts and invokes integration hooks. A standalone policy must gate calls, not merely hide rows. |
| `chonk-shell/src/desktop.rs:771,894-905` | Even Desktop construction/menu opening can read persisted wallpaper or probe an external integration. `following=None` alone does not disable all those calls. |
| `chonk-shell/src/shell.rs:290-335,2015-2030,2864-2874` | Both terminal actions ultimately default to foot. The configured-terminal path detaches and returns no tracked handle. Changing only `SessionState.terminal` is insufficient. |
| `wm-wayland/src/state.rs:3653-3693,5132-5155` | Existing integration provides the model: initialize shell policy/workarea/default grabs, feed notifications and root/shell input, then tick the shell. Reuse this contract without importing the Linux renderer or IPC stack. |
| `wm-theme/src/raster.rs:94-124,251-259` | One `FontState` can be shared by shell and window chrome. |
| `chonk-ui/Cargo.toml:8-14`, `chonk-about/Cargo.toml:8-10` | Existing About is an X11-default application, not a ready native Wayland app. Do not advertise it in the first Darwin catalog. |

## Ownership and file scope for later implementation

No implementation is authorized by this document itself. After design review,
stage a separate copy derived from the saved source; keep deployed sources,
qualified clients, binary, patches and all captured evidence unchanged.

| Proposed target | Responsibility |
| --- | --- |
| `crates/wm-darwin/{Cargo.toml,src/server.rs,src/main.rs}` | Opt-in desktop-shell entry, actual Shell ownership, lifecycle, event-loop integration. Preserve the existing no-shell mode. |
| `crates/wm-darwin/src/{scene.rs,input.rs}` plus small `shell_input.rs` | CPU PopupHost, real shell/root hit-testing and modal delivery; preserve ordinary Wayland/WM input. |
| New `crates/wm-darwin/src/{session.rs,applications.rs}` | Strict standalone options, owned application handles, request/result bridge and shutdown. No hardware recovery code. |
| `crates/chonk-shell/src/{shell.rs,desktop.rs,startup.rs}` plus a small `profile.rs` | Additive standalone profile and managed-launch API, real menu capability filtering, side-effect gating. Generic Linux/X11/Wayland constructors retain their existing behavior. |
| `crates/chonk-shell/src/spawn.rs` | Prefer leaving legacy launch/reaper behavior unchanged. Standalone requests bypass it completely. Any required shared type extraction must preserve generic behavior and be separately reviewed. |
| Separate terminal application project, derived from `client-terminal-native-startup` | Application lifecycle/identity contract below; preserve the startup observer and all earlier terminal projects. |
| New focused tests in the isolated copies | Actual Shell/Wayland protocol tests and process lifecycle tests. No claim of native runtime from host tests. |

Root owns the Darwin `chonk-ipc` platform port. Its reported 11 Linux-only
compile errors are a build dependency, not permission to stub successful IPC.
Do not concurrently edit `chonk-ipc`, any Omarchy-specific source, root's build
recipes, active backend tree, host, packet, or kernel. Root must supply the
reviewed IPC source identity before the combined native build. A standalone
session may intentionally have no control listener while the crate remains a
transitive compile dependency; disabled is not a functioning IPC service.

## Standalone profile and API contract

The following are **proposed new APIs**, not existing callable functions:

* `StandaloneDarwinOptions::validate(...) -> Result<ValidatedOptions, Error>`:
  validates explicit runtime/socket, session identifier, finite deadline and
  terminal executable selection. Nothing opens input or PDGOP on a bad option.
* `SessionState::standalone_darwin(...) -> Result<SessionState, Error>`:
  pure construction from explicit values, without generic config resolution,
  environment-driven theme discovery or ambient home-directory reads.
* `Shell::new_standalone(backend, state, fonts, policy) -> Result<Self, Error>`:
  constructs the **same Shell type** through shared internal initialization,
  with optional services genuinely absent. `Shell::new` preserves its legacy
  full profile and signature. No separate Darwin menu implementation.
* `Shell::take_platform_request() -> Option<PlatformRequest>` and
  `Shell::complete_platform_request(id, result)`: bounded request/result bridge
  for managed terminal launches. Enqueuing a request is not launch success.
  `Action::SpawnTerminal` and `RootMenuAction::LaunchTerminal` use this same
  bridge in standalone mode; their generic paths remain unchanged.
* `Shell::begin_shutdown(&mut wm)`: closes real transient UI and retires its
  input ownership before backend destruction. It is idempotent and performs
  no standalone persistence or integration calls.

These seams may be renamed during implementation; their semantics and tests
must remain. Keep the request queue small (maximum four pending requests) and
limit this first session to two live terminal children. Exceeding either bound
is a visible refusal, not a silently dropped or successful launch. Use monotonic
request/application IDs, never a title or arbitrary client-supplied PID as an
authority to signal a process.

The standalone policy is immutable after validation. Fixed settings: built-in
NeXTSTEP Classic, scale 1, click-to-focus, single-output freeform layout,
fixed reviewed US XKB configuration and the existing pointer mapping. Existing
native record validation and discontinuity refusal remain intact. `Both` is
required for this increment; no fabricated mouse and no host-origin input path.

First UI actions: **Terminal**, **End Session**, close, and native wm-core
Alt+Tab/Alt+Shift+Tab switching. Titlebar move and edge resize remain genuine
wm-core operations. Use one fixed launch chord (Super+Return) and close chord
(Super+Q), exercised through Shell's action resolution. The actual menu and
decoration capability filters must omit unsupported maximize/fullscreen,
minimize-without-restore, restart, capture, force-kill, arbitrary commands,
application discovery, About and external system controls. Hiding a menu row
alone is not an authorization boundary: stale/programmatic actions are also
refused by the standalone action policy. Normal xdg unsupported requests keep
their existing truthful refusal behavior.

The first application catalog is fixed to the real native terminal. It may
appear only once (Terminal row); no empty or fabricated Applications catalog.
Do not add a decorative fake dock. A later catalog can use actual AppIndex
entries only after its executables and protocols are qualified.

### Services explicitly absent in standalone mode

Represent absent services as `None`/an explicit disabled variant; do not create
fake successful listeners, watchers or session stores. Gate both constructor
and later tick/action/apply/shutdown paths:

* No Omarchy availability probes, menu loading, shell/bar hosting, theme
  adoption, following, configuration reads or commands. Do not edit those
  integration modules; the profile prevents entering them at shared call sites.
* No Hyprland/config import, auto-reload, marker polling, external appearance
  publishing, GSettings, or inherited session-environment mutation.
* No app scan/rescanner, autostart, restore/relaunch, layout persistence,
  wallpaper preference load/save, external widgets, network/system probes,
  IPC listener or externally driven actions. Keep purely in-memory desktop
  layout, cascade timers and wm-core notification handling active.
* No call to generic foot/configured-terminal/detached launch helpers. No
  executable PATH fallback. Environment values cannot reactivate a service.

Add an explicit menu policy to the existing `Desktop`/`root_menu_items` path;
the standalone menu must not call the availability probe at `desktop.rs:894`.
Initialize its wallpaper directly from the fixed built-in theme. Generic menu
construction remains unchanged. Tests must use a side-effect spy to prove
zero calls across construction, tick, menu opening, action refusal and exit.

## Fonts and configuration

Create one `FontState` before Shell/window-manager construction and pass clones
to `RasterThemeEngine::with_fonts` and the actual Shell. Font borrows are short,
single-threaded and never held across callbacks. Require packaged, licensed,
hash-bound fonts matching the chosen theme (including usable menu/title ASCII),
using the existing guest font inputs or a separately reviewed explicit font
load. Missing required glyphs/fonts is a preflight error; boxes or fabricated
labels do not qualify a menu. The terminal retains its licensed embedded font.
Root owns packaging and source/license binding; no downloads are needed here.

Runtime and application directories are private and owned, mode0700; logs and
records are0600. Use an explicit finite session deadline, not a new daemon or
login manager. Resolve config once before child creation. Do not read personal
Linux configuration, a previous machine's home, or builder store paths. The
new profile must reject conflicting options rather than silently overriding
them. The desktop's advertised keyboard settings must equal the real XKB
state: implement/validate the `set_keyboard_config` boundary instead of relying
on Backend's no-op default. Unsupported pointer settings are not advertised.

## CPU shell surfaces and input routing

Keep server-owned CPU pixels and the existing presenter. Rename the private
scene `Shell` record if needed to distinguish a surface from the actual Shell.
Use a dedicated hit-target type: `Root`, `Shell { id, local }`,
`Frame { id, local }`, `Client { id, local }`. Do not pretend an internal menu
is a Wayland client or overload a normal client ID with a root sentinel.

Hit-testing must mirror actual paint order: topmost mapped above-client shell
surface, windows/frames from top to bottom, mapped below-client shell surface,
then root. A shell hit consumes pointer input; it cannot leak into a covered
client. Bounds arithmetic is checked; hidden/destroyed/released surfaces never
hit. Popup IDs share the existing ShellId allocation space, with no stale ID
reuse within a session. A popup allocates/copies its pixels, maps and raises
before returning `Some(id)`; allocation failure returns `None` before a grab.
Destroy removes storage, stacking and pending targeted events idempotently.
The cascade owns a grab for its whole menu session: destroying one submenu
must not release that session's grab. Final close can destroy the remaining
surfaces before returning the retained token, as the existing cascade does.

Implement real `PopupHost::{create_popup,paint_popup,destroy_popup}` and
pointer/keyboard grabs. Retain the cascade's token; release only the matching
owner. Separate pointer ownership states are required for wm-core drag,
Smithay client implicit grab, and internal popup. Do not open a popup over an
unrelated active drag/client button grab; refuse/defer that gesture before
calling the infallible `grab_pointer` API. Nested modal ownership is refused
by the controller, not converted into a fake token. Failure after popup setup
closes the cascade and releases its actual ownership exactly once.

Implement `Backend::take_shell_click/motion/scroll` and the actual keyboard
grab methods used by wm-core. Bounded ordered queues must not drop release
events; overflow faults the session. Route root **presses** to
`Shell::on_root_press`; root releases still reach the shell for a menu/drag
that began there. Feed shell clicks, scroll, and motion to the actual Shell
APIs, drain `wm.take_notification()` to `Shell::on_notification`, and run
`Shell::tick` for cascade timers and switcher UI. `wm.bind_default_keys()` and
the profile's Shell bindings must be installed exactly once.

For keyboard input, update actual XKB state for every valid down/up. Resolve
Shell `KeyResolution::{Action,Menu,Consumed}` before wm-core fallback; a miss
during a wm-core switcher still reaches `BackendEvent::KeyPress/KeyRelease`.
Alt release must terminate the switcher. A menu owns all non-capture input;
capture is disabled here. Ordinary client keys continue through Smithay.
Keep per-press ownership until release, so a shortcut or menu key never leaks
its release into a client and a previously forwarded key is not left stuck.
Do not invent HID records or key releases to hide a missing sequence.

Modal focus rules must be explicit: suspend client keyboard focus while the
shell owns it; on close, restore only a still-mapped valid client. If
modal-consumed keys remain held, defer client focus restoration until those
keys are released, rather than introducing them in a Wayland enter array.
Continue processing physical/XKB state while deferred. Tests must include
held modifiers, release outside the menu, closing the focused client, switching
between windows, cancellation, and shutdown while keys/buttons remain down.
No shutdown waits indefinitely for a human release.

The event loop remains the sole owner. Perform action/notification effects
after a Smithay callback's borrow has ended, and before routing the next input
event that depends on those effects. Do not enqueue an entire batch under the
old modal state. No callback launches a process or blocks waiting for one.
Reconcile focus and shell state before repaint. Preserve SHM copy/release,
frame-callback-after-present, mapped-pixel bounds, WC stores and ownership
fence. No event-loop or render optimization is included.

## Real terminal launch and process lifetime

Create a separately named **application** target derived from the reviewed
native terminal source; do not overwrite the startup observer or relabel its
receipts. Retain its actual PTY/session/exec checks, XKB translation, grid,
SHM/SSD lifetime and unconditional cleanup on close/disconnect/error. The
initial application remains `/bin/sh -i +m`, TERM=dumb, no detached/background
job support, no claim of full xterm/editor compatibility. Do not relax the
foreground-group check by assumption. A changed job-control contract is later
work. Application output is never painted from expected proof strings.

The session passes explicit socket, unique application ID/private record
directory and remaining lifetime. A unique48-hex nonce remains acceptable as
an audit identity; it must not force a fixed test command or expected response.
Window titles identify actual terminal instances, not the obsolete startup
observer. Each launch has separate PTY, logs, frame state and child ownership.
The second terminal must not collide with the first's create-new record path.
The application emits checked exec/readiness, protocol and cleanup records;
these report facts, not physical typing or a completed desktop milestone.

`OwnedApplications` in the compositor owns each `std::process::Child` from
spawn to reap. One thread performs `try_wait`, TERM/KILL decisions and state
transitions; no detached reaper, global SIGCHLD-ignore or competing waitpid.
Use an absolute executable and argv directly, without `/bin/sh -c`, PATH
search or a generic `-e` translation. Child stdin is not the console; stdout
and stderr go to bounded private per-instance diagnostics. Do not pass HID,
listener, client, or framebuffer descriptors to children; retain CLOEXEC and
verify the exec descriptor boundary with a child fixture. Pass only the
explicit Wayland/runtime/XKB environment and the per-launch audit contract.
The terminal opens its own Wayland connection and owns only its own PTY.
Retain an ID-to-Child map; do not signal a bare PID after reap. Launch failure
(including missing executable/permissions/exec or child setup failure) produces
an actual error record and visible existing-shell status/menu row, never a
fabricated mapped window. Both menu and key launch actions receive the same
request result. A child process alone is not a successfully displayed app:
track Spawned, CheckedReady, Mapped, Exited and CleanupConfirmed separately.

All application-to-session status is bounded and tied to that launched child
and its private instance path/channel. A title/app_id/PID claimed by an
unrelated Wayland client cannot authorize a kill or satisfy readiness. Do not
require root's IPC port to invent peer PID support: this increment tracks
processes through its own Child handles. Host wire tests can use explicit test
client credentials; production correlation must be qualified using available
Darwin credentials or an inherited private per-launch channel before treating
a particular surface as the child for anything beyond display bookkeeping.

The shell's pending request queue is drained once per event turn. Spawn errors
do not trigger automatic retries. App close uses real xdg close; the terminal
then runs its existing idempotent PTY cleanup/reap. There is no PID inferred
from an arbitrary surface to force-close. Starting a third live terminal is a
visible resource-limit refusal; after a child is reaped its slot becomes free.
Do not promise a hard in-process bound for an OS spawn/exec syscall itself.
Avoid custom blocking pre-exec hooks; retain the root-owned host timeout for a
guest that stops returning. A host-forced stop cannot become a clean session.

## End Session and failure handling

State machine: `Preflight -> Running -> ClosingApplications -> ClosingDisplay
-> Exited`, with sticky failure information carried through shutdown.

1. Menu End Session, SIGTERM/SIGINT, finite deadline, protocol/input fault or
   presenter failure stops new launches and rejects subsequent launch actions.
   Cancel transient shell UI and release its modal ownership. Continue bounded
   client dispatch while already owned applications shut down.
2. Ask mapped applications to close, then TERM still-live **owned** Child
   handles at a fixed deadline. Each terminal owns and must reap its own PTY
   child/session. Collect its actual cleanup result and wait status; interactive
   shells terminated deliberately need not exit0.
3. A final bounded KILL/reap of an unresponsive terminal process may be needed.
   Reaping that process does **not** prove its separately sessioned PTY child
   was cleaned up. Missing terminal cleanup acknowledgement is a sticky
   `CLEANUP_INCOMPLETE` result, never normal End Session success. Do not adopt or
   signal arbitrary descendant PIDs. Root's external bounded recovery remains
   the final boundary for a hung guest/process; no new reset path lives here.
4. Invoke profile-aware Shell shutdown; destroy internal surfaces, cancel
   pending callbacks, disconnect clients and remove the owned Wayland socket.
   Stop all drawing before closing PDGOP. Keep native HID descriptors owned
   until graphics release, then close them to restore console input. No async
   renderer or callback can retain the presenter after close.
5. Write a final private result with every application outcome and error,
   compositor cleanup and deadline cause. Missing evidence stays missing.
   Return nonzero for incomplete cleanup or a fatal session fault, even if
   root later restores Linux. A launch failure can leave the desktop usable,
   but the failed launch record must remain.

Proposed qualification limits: at most two live applications, no automatic
restart, and a shutdown budget reserved inside the existing root-selected
outer deadline. Concrete grace/TERM/KILL values must fit that packet's timings;
they are not hardware authorization. Preflight checks occur before acquisition.
After partial acquisition, RAII/error paths close only resources this session
actually owns, including a keyboard FD if mouse open fails.

## Host tests required before a native build

Tests must execute the real Shell, CPU scene, Wayland server/client and process
logic where applicable. A memory presenter and synthetic input are explicitly
host-only fixtures, never native evidence. Existing backend/terminal tests
remain; no broad testing/build is performed by this design task.

1. **Profile side effects:** recording service factory sees zero integration,
   config/home scans, IPC binds, app scans, persistence, autostart or detached
   spawn calls during construction/tick/menu/action/exit. Legacy profile
   behavior remains covered. Negative control re-enabling any prohibited call
   must fail. Unsupported menu/action injection cannot execute a program.
2. **Actual shell drawing:** construct Shell over CPU backend with real
   FontState/theme; open and navigate a real cascade. Its rendered pixels,
   geometry and hit regions agree. Failed popup allocation leaves no grab.
   No copied screenshots or hand-painted menu fixture satisfies this test.
3. **Input ownership:** real Wayland test clients plus native-format synthetic
   records verify menu key/click isolation, root press/release, outside
   dismissal, token mismatch, nesting refusal, hidden/destroyed surface
   refusal, mixed shell/client stacking and exact-once button/key ownership.
   Alt+Tab cycles two real surfaces and Alt release commits; Escape cancels.
   Client destruction during modal input causes no stale focus/use-after-free.
4. **Launch bridge:** menu and Super+Return each produce one identical typed
   request. Missing executable, exec failure, readiness timeout, bad private
   record and third-live-app limit are visible failures without a fake window.
   Use real bounded host child processes to check sole-owner wait/reap and
   no late signal after reap; fakes only inject otherwise impractical faults.
5. **Two actual clients:** independent Wayland connections/SHM/SSD configures,
   title identities and PTYs; focus routes text to exactly one shell. Move,
   resize, close and reopen go through wm-core/Shell. Closing one app must not
   close the socket, steal the other's input or destroy its retained pixels.
6. **Terminal:** preserve child setup/exec failure tests, PTY raw transcript
   integrity, fixed buffer ownership, resize and disconnect cleanup. Distinct
   audit paths and no hardcoded command/output. TERM=dumb/foreground refusal
   limits are exercised rather than bypassed.
7. **Shutdown:** empty session, normal app close, End Session with two apps,
   held input, EOF, launch-in-flight, app ignoring TERM, presenter error and
   repeated stop signals. Simulated/actual missing PTY cleanup confirmation
   remains incomplete even after application reap. No draw after PDGOP-close
   marker; all modal tokens/queues/FDs retire once. Unknown application state
   cannot produce a successful cleanup record.
8. **Source/build separation:** source diff forbids changes to kernels,
   transport/PIO/protocol, frozen clients, host/controller, Omarchy modules or
   Linux backend. Root's reviewed IPC port is a separately hashed dependency.
   Host and Apple target checks are separate results. No runtime claim follows
   from either.

## Root handoff and runtime gates

After source review, root chooses the isolated implementation/SDK build and
packet. The combined native artifact must bind shell/profile changes, actual
terminal executable, fonts, IPC port, supervisor and unchanged native-input
kernel. Grok's independent two-window binary regression is useful evidence for
the existing backend; it does not replace this shell integration.

A later authorized cycle should establish a real menu, two separately launched
terminal processes/surfaces, focus routing and command output, native pointer
move/resize/close/relaunch, then owned shutdown, fresh console return and verified
Linux through the existing controller. Do not wait for a person or pretend a
scripted/host event was physical input. Human input, if voluntarily present,
is recorded separately. Preserve sealed host TX and the absence of PDSerialHID
in the native candidate. A menu snapshot or process list alone does not qualify
the workflow. Keep exact raw-HID correlation open, and do not make latency,
multitouch, GPU or Wi-Fi a gate for this scoped next step.
