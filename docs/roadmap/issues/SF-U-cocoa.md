# SF-U: Cocoa stack (Mac GUI applications on the SiliconForest desktop)

## Hand-over (12 November, stream parked by the owner; read this first)
The Cocoa stream is parked until the system boots from disk. Everything proven is committed:
lab repo `e829293fc` and the commit that adds this section (branch `pcie-wifi`); compositor
worktree `runs/cocoa/chonkstep-worktree`, branch `wm-darwin-cocoa`, `f0955fb` (local only: never
push that repository). Nothing of this stream is running.

State, all in QEMU arm64 on the pin (`e7aa00a6`), nothing on the M1:
- Popups: done for AppKit applications, 5 of 5 at 100, 150, 200 and 250% (section "Popups",
  "Popup results"). After that change: `hello` at 200%, `frame` at 100%, `apps` at 150% pass
  (`reg1-hello-s2`, `reg1-frame-s1`, `reg1-apps-s1.5`). The Firefox checks, `live-scale`,
  `miniaturize`, `zoom` and the scorecard were NOT re-run on patches 0016 to 0018.
- Everything else is as the sections below say (Firefox 160 with its sandbox on, Auto Layout,
  macOS 26 controls, scorecard 1214 of 2077).

Commands:
```sh
integrations/puredarwin/cocoa/cocoa_check.sh popups --scale 2      # one run, exit 0 = pass; 1, 1.5, 2, 2.5
python3 integrations/puredarwin/cocoa/checks/popups.gen.py         # regenerates checks/popups.json
runs/cocoa/rep.sh popups NAME 2 5                                  # scratch: 5 runs (first alone, then 3 at a time)
bash runs/cocoa/build-compositor.sh                                # compositor from the worktree into runs/cocoa/target
runs/cocoa/run-ff.sh firefox-sandbox-page NAME $(nix-build ... build-firefox-root.nix)   # a Firefox check
```
Patches are made from trees under `runs/cocoa/patchwork/` (not committed): `a/` the pin, frozen
trees after each set of patches (`c/` after 0015, `d/` 0018, `e/` 0027, `f/` 0033, `g/` 0039; the
later sets are the Terminal.app stream's, docs/roadmap/issues/SF-Y-terminal-app.md), `b/` now. Edit
`b/`, then `bash integrations/puredarwin/cocoa/mkpatches.sh` (the script names which file belongs
to which patch; within one set a file belongs to one patch). If that directory is lost, `a/` is
the pin's files, each frozen tree the pin with the patches up to its number applied, and `b/` the
pin with every patch of `patched.nix` applied.

Traps met (keep the checks honest):
- Never run a batch while the workstation is loaded: at a load of 40 the desktop itself fails
  (`result.json` has `host_load` and `times.session`: about 25 s is normal).
- Parallel runs race on the image base: the first run of a new root must run alone.
- A check that stops early never reaches its "no crash" step: look for `COCOA_APP_EXIT 1 rc=`.
  `DYLD_INSERT_LIBRARIES=/usr/lib/libhbcrash.dylib` (Homebrew stream's guest tools as a `--tree`)
  gives a backtrace.
- Do not edit `cocoa_check.sh` or a check's JSON while a batch runs.
- The reference Mac shows nothing on its screen: menus cannot be measured there.

Next, in this order:
1. Firefox's own popups: context menu (native `NSMenu`: XUL uses `popUpMenuPositioningItem:`,
   `menuWillOpen:`, `menuDidClose:`, `menu:willHighlightItem:`, `cancelTracking`, all present),
   hamburger panel and other XUL panels (borderless windows at the pop-up level: they become
   grabbing `xdg_popup`s; what a compositor dismissal must turn into for Firefox to roll them up
   is not worked out), `<select>`. `checks/firefox-popups.json` is written and has NEVER been run;
   `build-firefox-root.nix` has not been built with 0016 to 0018.
2. Port to upstream's tip, now the lab default (`docs/roadmap/issues/SF-V-rebase.md`). Per the
   rebase stream: apply as they are 0002, 0003, 0008, 0009, 0013, 0014, 0015, 0016; already
   upstream 0001, 0007, 0010 (except one CMakeLists hunk); fail 0004, 0005, 0006, 0011 (passes with
   fuzz), 0012, 0017, 0018. Then every check and the scorecard again, differences from the pin
   reported.
3. Menu presentation proposal with mock-ups (section "Menus"). Notes for it: AppKit already
   serialises the main menu (`pdSerializeMenu`, `activateMenuItemAtPath:forWindow:` in
   NSApplication.m); a strip can be drawn by `wm-darwin` as the top of the area `wm-core` takes for
   the client (no theme engine change), titles from `wm-theme`; a title's menu can be the
   application's own popup (`-[NSMenu _pdTrackAtScreenPoint:...]` with the window as parent), so
   it is the same menu as every other; the transport is a small private Wayland protocol
   (tree as JSON through a descriptor, `activate(path)`, `show(path, x, y)`). Nothing built.
4. Fonts (CoreText enumeration and matching, open fonts by recipe, `/System/Library/Fonts`,
   tracking table, weights).
5. Video in Firefox through the software path of `m1-avd/videotoolbox`.
6. Quality items (dark, pressed, disabled states; focus ring; layout guides; rest of the suite;
   the menu look; lists taller than the screen).
7. Electron blank window (remote layers; the WindowServer already has
   `_windowServerExportSurface`/`_windowServerImportSubsurface` over `pd_surface_host_v1`).

## 13 November: part of this stack is on the M1's disk (for terminal programs)
The gap job of issue 45 (`SF-S-homebrew.md`, "Framework gaps") needed `AppKit.framework/Versions/C`
for Homebrew's vim and did not write a second AppKit: it took this stream's root.
- What: `integrations/puredarwin/macos-compat/build-appkit.nix` = the files of
  `build-desktop-root.nix` (the pin with patches 0001 to 0018) that dyld loads for a program linking
  AppKit or Cocoa: the bundles AppKit, Cocoa, CoreData, CoreGraphics, CoreText, CoreVideo, IOSurface,
  Onyx2D, OpenGL, QuartzCore, Carbon, and `libWindowServer`, `libGL`, `libGLU`, `libgallium`,
  `libLLVM`, `libdrm`, `libwayland-client`, `libwayland-cursor`, `libfreetype`, `libfontconfig`
  (101 MB). Not taken: the applications, launchd jobs, fonts, fontconfig's files, the rest of Mesa,
  the Swift runtime, and this root's CoreFoundation, Foundation, CoreServices and IOKit (the disk
  root keeps its own, built from the tip).
- Evidence for the route: the scanner (`homebrew/sf_gapscan.py`) finds every import of AppKit's
  whole load closure (35 images) on the M1's tip-built base; in QEMU on the M1's root image and on
  the M1 itself a windowless program gets `+[NSPasteboard generalPasteboard]`, sets and reads a
  string in 27 ms, and vim starts in 88 ms with the stack loaded (libLLVM and Mesa included: they
  cost no visible time). So pin-built frameworks on the tip-built base load and run for this use.
- What this is not: nothing with a window was run from the disk. No application, no launchd job
  and no font was installed, the compositor on the disk is not this stream's branch, and
  AppKit's own resources were not exercised. The M1 plan below still stands; the frameworks are
  now already where it needs them.
- Found on the way: in a process that never opened a window the general pasteboard is private to
  the process (`_windowServerSelectionGeneration()` and friends return at `!ws.connected`), so
  vim's `"*` register does not reach the desktop's clipboard. A real connection needs a
  data-control protocol in the compositor (a Wayland client without keyboard focus gets no
  selection).

## Goal
Mac GUI applications run on the SiliconForest desktop (Chonkstep) using PureDarwin's open
reimplementation of Apple's application frameworks: first a trivial AppKit application, then
upstream's own applications, then the official macOS Firefox. Owner's requirements for Mac
windows on our desktop (11 November): our own window frame and controls, in every theme
("not to be forced into using the AppKit window controls"), and rendering at the desktop's
scale. Work is done in QEMU on the workstation (arm64 `virt`, TCG; the QEMU stream's
environment); the M1 proof comes later.

Tools and recipes: `integrations/puredarwin/cocoa/`. Run output: `runs/cocoa/` (not committed).
Compositor changes: branch `wm-darwin-cocoa` of `sources/chonkstep-darwin` (worktree
`runs/cocoa/chonkstep-worktree`, local only).

## Upstream state (checked 11 November)
- The development branch of github.com/PureDarwin/PureDarwin is `upstream-xnu-12377` (there is
  no `next`); our pin `e7aa00a6` (6 Nov) is on it, the tip was `c713d8fb` (11 Nov), 119 commits
  later. Fresh clone: `sources/puredarwin-upstream-next`.
- No Firefox image, recipe or download exists in the tree. What exists is the framework work
  "for the official Firefox build" (19 Oct, in our pin), tests (`tests/appkit-ff`,
  `tests/frameworks-ff`, `tests/cg-ff`; at the tip also `tests/ffprobe`), and the website's
  statement that "a handful of unmodified macOS apps run as well, Firefox among them" on the
  x86_64 Argon desktop (`neuswc` compositor).
- The WindowServer is a library inside every application and a plain Wayland client. It
  needs wl_compositor, xdg_wm_base and wl_shm; everything else is optional (layer-shell for
  desktop-level windows, `pd_window_position_v1` for absolute positions, `pd_surface_host_v1`
  for layers hosted across processes, subsurfaces, dmabuf, data device, output management).
  Every NSWindow is an xdg_toplevel. Upstream has no display scaling at all (backing scale is
  the constant 1 at the pin and at the tip) and always draws its own title bar.
- The pin already has arm64 builds of the whole stack in its flake (`appkit-msm-arm64`,
  `terminal-msm-arm64`, `calculator-msm-arm64`, `argon-desktop-msm-arm64`, ...), made for a
  Qualcomm board. The tree generates its own SDK; no Apple SDK is needed.
- Decision: no rebase of the lab for this stream. Tip commits are taken as separate patches
  (`patches/0001-tip-*`), so they drop out if the lab is rebased.

## Upstream's own result, reproduced as far as it went (x86_64, KVM)
Run by a sub-agent under `runs/cocoa/upstream-x86/` until it was cut off; the facts below were
read from its files and its still-running guest.
- Built at the pin: upstream's x86_64 Argon desktop image with Firefox added
  (`scripts/ff-image.nix`, images `img/result-argon`, `img/result-argon-ff`). It boots under KVM
  to the Argon desktop: `run5/shots/ff158-final-by-parent.png` (menu bar, Dock, Terminal).
  Earlier boots failed in three ways before one worked (`logs/run1..4-*.log`: a spinlock
  panic, a hang with 2 CPUs, an xHCI spin).
- Firefox 160.0 (current official) on that image: refused by dyld.
  `Symbol not found: _ASWebAuthenticationSessionErrorDomain ... Expected in: AuthenticationServices`
  (guest `/var/log/ff-a2.log`). The same symbol is missing at upstream's tip.
- Firefox 158.0 (the version upstream's tests name): XUL loads and runs. Seen: a sandbox
  profile that upstream's libsandbox cannot parse (`sandbox_init() failed with error "line 10:
  define expects a name and a value"`), `NSApplication: published 1 menu items for 2 windows
  to the menu bar`, kernel `EXC_GUARD` reports for the firefox process, two child processes
  dying, then Mozilla's crash reporter starting and panicking on `Class with name NSStackView
  could not be found`. No Firefox window was on the screen. NSStackView and sandbox-profile
  work are in upstream's tip (10 and 11 November), so the claim on the website is presumably
  true of the tip, not of our pin. The tip image was not built: it needs 346 derivations here
  (toolchain, LLVM, kernel, Wine: everything), against 171 for the pin.
- Not done: the tip, Firefox with the sandbox switched off, any page render.

## Design: Mac applications as first-class windows
A Mac application is an ordinary Wayland client of Chonkstep (the WindowServer library inside
it). The standard protocols carry everything; nothing of upstream's desktop (`neuswc`'s
private protocols, layer-shell, MenuBar.app) is needed, and the compositor needed one small
addition. The alternative, teaching Chonkstep upstream's `pd_*` protocols and layer-shell, would
have reproduced upstream's Argon look inside our desktop: AppKit's title bars and controls and
a second menu bar, which is what the owner does not want.

### Frames (patches 0004, 0005; compositor a447cef)
- Every titled NSWindow asks for a server-side decoration (xdg-decoration) and AppKit lays it
  out as content only: no title bar, no traffic lights, no resize bands of its own. Chonkstep
  draws its frame around it exactly as around a terminal, from the active theme, at the display
  scale. That includes windows with a full-size content view (content under the title bar, as
  Calculator and Firefox have): their content starts below our title bar.
- Borderless windows (menus, tool tips), sheets and full screen windows have no standard frame
  and ask for client-side decoration.
- A compositor without xdg-decoration (upstream's `neuswc`), or one that answers client-side,
  gets AppKit's own frame as before (`PD_WS_CLIENT_FRAME=1` forces that for a test).
- Our frame's buttons keep Mac semantics. Close is xdg_toplevel.close -> `performClose:`, so
  the application is asked (`windowShouldClose:`) and may refuse. Miniaturize and maximize are
  the compositor's own operations; the compositor now tells the client through the toplevel
  states (maximized, suspended, fullscreen) and NSWindow follows: `isZoomed`,
  `isMiniaturized`, `windowDidMiniaturize:`/`windowDidDeminiaturize:`, resize notifications.
- Title: xdg_toplevel.set_title. Minimum and maximum size (and min = max for a window that is
  not resizable): xdg_toplevel.set_min_size/set_max_size.
- Not conveyed, because xdg-shell has nothing for it: document-edited state, a window without
  a close button (our button is always there; a click is still answered by the application),
  the proxy icon.

### Scale (patches 0004, 0005, 0006)
- The WindowServer takes the compositor's preferred scale from wp_fractional_scale_v1 (in
  120ths), or the output's whole scale without it. AppKit works in points: the screen is the
  output's mode divided by the scale, window sizes, positions and pointer locations are points.
- A window's backing store is points x scale pixels; the context's base transform carries the
  scale, so paths, images and glyphs are rasterised at the device density. The buffer goes to
  the compositor with a wp_viewport destination in points: at 200% it is shown pixel for pixel.
- Fractional steps (125%, 150%, 175%, 250%) are true fractional backing, not "render at the
  next whole scale and shrink": the buffer is exactly points x scale and the compositor does
  not resample it. Measured below.
- A live change (the desktop menu's Display choice) arrives as a new preferred scale; every
  window remakes its backing store and redraws, `windowDidChangeBackingProperties:` and
  `viewDidChangeBackingProperties` are sent, `NSScreen.backingScaleFactor` follows.
- Upstream's text drawing ignored every scale (glyph bitmaps at the point size, only their
  positions scaled); fixed in Onyx2D (0006).
- Not scaled yet: layer-backed content (QuartzCore composites a layer tree at 1x and the
  window context enlarges it: correct size, soft), NSOpenGLView, the pointer image an
  application sets, cached image representations made at 1x.

## 13 November: what the Terminal.app stream changed in this stack (SF-Y, issue 52)
One stack: the changes are patches 0019 to 0026 of `cocoa/patches` on top of this stream's 0001 to
0018 (`patched.nix` lists what each does), made in the same patchwork tree (`d/` = this stream as
parked, `b/` = now; `mkpatches.sh`'s `mkterm` lines). They were run with Apple's Terminal on the M1
only; this stream's own checks (popups, Firefox, the scorecard) were NOT re-run on them.
- AppKit: 0019 (tip's NSStackView, NSAccessibilityElement), 0020 (names the macOS 26 SDK imports),
  0021 (nib loading, exceptions from run-loop callouts, an application's own fonts registered from
  `ATSApplicationFontsPath`), 0024 (`+[NSFont fontWithName:size:]` asks CoreText for names no
  installed face carries; a selection beyond the content; a failing binding is reported once).
- CoreText 0023: fixed-pitch names resolve to an installed fixed-pitch face (item 4 of "Next" above
  is touched: the alias table now holds several families per name, tried in order); the fixed-pitch
  UI font comes from the registry; two private functions Terminal imports.
- CoreGraphics 0025, Onyx2D 0026 (`-copyBitsInRect:toPoint:gState:` for the bitmap context:
  `-[NSView scrollRect:by:]` works), WindowServer 0022 (a diagnostic dump, size limits in the trace).
- Found and NOT fixed here: the layout solver of 0011 runs over the whole tree once per resized
  view (388 solves, 4.8 s, while Terminal's preferences nib is decoded: SF-Y's table).
- Foundation (not this stack, but every AppKit program sees it): key-value observing now follows
  key paths and hears setters called directly (`terminal-app/patches/foundation-macos-applications.patch`).
  Bindings that silently did nothing before now fire.
- Instruments that this stream can use: `terminal-app/probe/sf-trace.c` (methods of chosen classes
  as they are called, with arguments, results and times), the compositor's inject module
  (`terminal-app/compositor/wm-darwin-inject.patch`: requested key and pointer records and a capture
  of the real screen, on the M1 and in QEMU alike) with `terminal-app/tools/sf_inject.sh`.
- Menus: built on 14 November, see "Menus" below (it was: nothing built).

### 14 November: what the Terminal.app stream changed next (patches 0028 to 0032)
- AppKit 0028: the main menu as a strip in each window (section "Menus"); an application ends
  with its last window on such a desktop; a fixed-size window grows for its toolbar.
- AppKit 0029: `-[NSTabView removeTabViewItem:]` (selected item -1), nib toolbars (items lost when
  the delegate outlet was connected, visibility classes), `-[NSAttributedString URLAtIndex:effectiveRange:]`,
  `-[NSWorkspace typeOfFile:error:]` from the application's declared types, `-[NSCell interiorBackgroundStyle]`,
  `NSBeep()` says once that it makes no sound.
- WindowServer 0030: an app id for programs without a bundle identifier.
- AppKit 0031: the content bindings of NSPopUpButton (content, contentObjects, contentValues,
  selectedObject; the selection follows content that arrives later).
- AppKit 0032: the size of an unbounded one-style string is remembered (a measurement was 5 ms on
  the M1); a title up to an eighth too wide for its place is drawn narrower instead of cut.
- Foundation (every AppKit program sees it): key-value coding finds `-_key`, `-_getKey`, `-_isKey`
  and `-_setKey:` (AppKit's binding accessors: bindings of pop-up buttons, matrices and tab views
  now reach their views), scalar instance variables, subclasses of NSAttributedString.
- The compositor on the M1 has this stream's popup commits (`terminal-app/compositor/README.md`).
- Checks re-run on this stack: see "Results" (rows dated 14 November).

### Menus (built 14 November: proposal 1, drawn by AppKit)
The desktop has no global menu bar, so the main menu of a Mac application is the first row inside
the frame of each of its titled windows that is not a panel (`NSWindowMenuStrip.m`, patch 0028):
- The strip is the application's own `NSMenu` in a horizontal `NSMenuView`. A title's menu is the
  ordinary popup (an `xdg_popup` of the window, the same tracking loop as every other menu), so
  the compositor needs nothing beyond popups and no protocol was added. (The proposal had the
  compositor draw the strip from a serialised tree; drawing it in the client gives the same picture
  with the theme AppKit already has and keeps one menu implementation. The serialised tree for
  upstream's MenuBar.app is still published where that process exists.)
- The strip belongs to the frame: `-frameRectForContentRect:` and `-contentRectForFrameRect:` count
  it, so a window sized from its content keeps its content size (Terminal: 80x24 stays 80x24).
- Pointer: a press on a title opens its menu at once, the open menu follows the pointer along the
  bar, a second press on the open title closes it. Keyboard: Control-F2 (macOS: "move focus to the
  menu bar") opens the first menu; Left and Right go to the neighbouring menu, Up, Down, Return and
  Escape as in every menu. The application menu is shown under the application's name.
- Only where the compositor draws the frames (`NSWindowFrameIsDrawnByServer`); the default
  `PDWindowMenuStrip=NO` turns it off. On such a desktop an application without windows cannot be
  reached, so it ends (through `-terminate:`) 0.7 s after its last window closed
  (`PDQuitsWithLastWindow=NO` keeps it).
- Seen with Apple's Terminal on the M1 (`docs/roadmap/issues/SF-Y-terminal-app.md`); the `hello`
  check passes with it (its window has the strip). Not done: the application name in bold, a menu
  taller than the screen, proposal 3 (the menus as a branch of the desktop menu).

### Firefox's title bar
Firefox by default draws its tabs in the title bar (a full-size content view) and expects the
system's buttons floating over its top left corner. Options: (a) our frame around it, tabs
directly below our title bar, no AppKit buttons (what the frame rule above gives; Firefox
finds no standard buttons and leaves no gap for them); (b) the same with Firefox's own
"Title Bar" setting, which changes nothing further here; (c) client-side frame for full-size
content windows, i.e. Firefox's look with AppKit's traffic lights, which is the forced AppKit
controls again. (a) is what is built.

## Popups
Every popup AppKit shows is an `xdg_popup` of the window (or menu) it belongs to, placed and kept
on the panel by the compositor. One tracking loop serves them all. Pass rates: see "Popup results".

### Why choosing an item was flaky at 150% and 200% (found 12 November)
Not scaling. `-[NSDisplay discardEventsMatchingMask:beforeEvent:]` tested the type of the event it
was handed instead of each queued event, so it removed every event queued before that one. A
tracking loop's periodic timer calls it every 20 ms (to drop older periodic events), so a mouse-up
or key still waiting in the queue was thrown away. At higher scales one loop pass (a repaint)
takes longer than 20 ms under emulation, so the release was lost more often; at 100% rarely.
Upstream's bug, at the pin and at the tip. Patch 0016 (one condition). Evidence: baseline
`pb0-s1` pass, `pb0-s1.5`, `pb0-s2` fail at the choice; trace `pb1-s2` (the opening click's
release never reached the loop); with only 0016 the old check at 200% passes 5 of 5
(`pa-s2-r1..r5`).

### What is built (patches 0017 AppKit, 0018 WindowServer; compositor worktree)
- AppKit. Menus, context menus, submenus (any depth), pop-up buttons, pull-down buttons and combo
  box lists are all an `NSMenuView` in a borderless window at the pop-up menu level, run by one
  loop (`-[NSMenuView _trackFromEvent:]`). The older `NSPopUpView`/`NSComboBoxWindow` loops are no
  longer used by the controls.
  - Pointer: opened by a press, the menu follows the pointer and the release picks - unless the
    release comes within 0.4 s and within 3 points of the press (a click: the menu stays open for
    a second click). A release on a disabled item, a separator or a submenu title picks nothing.
    A submenu opens under the pointer and stays 0.25 s after the pointer went on to another item
    (a slanted move into it arrives). A press outside every menu dismisses and is not passed on.
  - Keyboard: Up/Down over the items that can be chosen (no wrap), Right opens a submenu, Left
    closes one, Return/Enter/Space choose or open, Escape and Command-period dismiss everything,
    Home/End/Page Up/Page Down; typing selects by title prefix (1 s pause starts a new prefix).
    Auto-repeat is honoured for Up and Down only.
  - A dismissed menu sends nothing. (macOS: Apple documents that a pop-up button sends its action
    when the user chooses an item; this is taken from the documentation, not measured: tracking a
    menu on the reference Mac would put it on that Mac's screen, which the harness does not allow.)
  - The menu's delegate hears `menuNeedsUpdate:`, `menuWillOpen:`, `menuDidClose:` and
    `menu:willHighlightItem:`; `cancelTracking` ends the loop. A column for check marks appears
    when an item of the menu is checked; hidden items take no room.
  - Pop-up button: the selected item lies over the button with its title where the button's is,
    checked; at least as wide as the button. Pull-down: under the button, without its title item.
    Combo box: under the field, the current value lit; a choice sets the value and sends the
    action also when the field is not being edited (upstream did neither then).
  - Tool tips take no pointer events, so they are popups without a grab.
- WindowServer. `kWSWindowSetPopupAnchor` gives a popup its parent, an anchor rectangle and a kind
  before it is shown: at its frame (slides along both axes), beside the anchor (a submenu: flips
  to the other side, slides vertically; a submenu of a flipped menu goes on in the same direction),
  below the anchor (a list: flips above). The xdg_positioner is built so that an unconstrained
  popup lies exactly at AppKit's frame; no horizontal offset is used for submenus because a
  positioner's offset is not mirrored by a flip. Where the compositor put the popup comes back
  (`WS_EVENT_POPUP_PLACED`) and becomes the NSWindow's frame, so a menu and its submenus are
  hit-tested as they lie. A held button is kept when the pointer crosses from the window onto the
  menu (press-drag-release). A popup that was flushed before it was ordered in is presented (tool
  tips never appeared), and never with the picture of the popup that had the same number before.
  `_windowServerPlacesPopups()` tells AppKit to leave all clamping to the compositor.
- Compositor (worktree, see commits there): the pointer is delivered again where it is when a popup
  appears, moves or goes under it; a repositioned popup keeps its picture.
- Upstream faults fixed on the way, besides 0016: `NSPopUpView` array init, list margins, screen
  against window coordinates (0015, first agent); combo box index held in a 32-bit `unsigned`
  compared with `NSNotFound` (crash on dismissal); combo box ignored a choice while not editing.

### Popup results (QEMU arm64, executed 12 November)
`cocoa_check.sh popups --scale S`: the test application `popups/` (pop-up button, pull-down
button, combo box, a view with a context menu two submenus deep, a button with a tool tip) driven
through `checks/popups.json` (207 steps, 60 named checks; generated by `checks/popups.gen.py`).
What it proves per run: press-drag-release; click-click; a second opening placed on the selected
item; a disabled item cannot be chosen; Escape, a press on the application's own window (which
does not reach the view) and a press on another application's window all dismiss without an
action; Down to the end over a disabled item, Up to the start, type-select, Return and Space;
pull-down; combo box by pointer and keyboard; context menu; More > Deeper > Second Leaf with the
pointer; the same with the keyboard including Left; Escape inside a submenu; the submenu on the
side that has room with a two point overlap; every popup on the screen; the tool tip as a popup
without a grab that goes when the pointer leaves; the window works afterwards; every menu that
began tracking ended; every popup the compositor was given is gone; no crash.

| Scale | Final code | Runs |
|---|---|---|
| 100% | 5 of 5 pass | `pt-s1-r1..r5` |
| 150% | 5 of 5 pass | `pt-s1.5-r1..r5` |
| 200% | 5 of 5 pass | `pt-s2-r1..r5` |
| 250% | 5 of 5 pass | `pu-s2.5-r1..r5` |

Screenshots: `runs/cocoa/pt-s2-r1/shots/` (01-popup, 02-popup-typed-g, 03-pulldown, 04-combo,
05-context, 06-submenus, 07-tooltip). At 100% the window sits in the screen's bottom right corner,
so the context menu is slid up and the submenu flipped to the left there; at 150% it sits at the
top, so the pop-up list is slid down.

How it got there, kept because each step was a real fault:
- Batch `pq` (0 of 20): a combo box choice sent no action (my change), and the check raised the
  terminal over the application. Batch `pr` (0 of 20): the application crashed in `-[NSWindow
  dealloc]`: the window kept its shared memory path as an autoreleased string and read it after
  it was gone (upstream; found with `libhbcrash`, run `pcr1`). Batch `ps` (19 of 20): at 250% one
  opening click was taken for a press-and-hold because its release reached the application more
  than 0.4 s after the press; the limit is now the double-click interval (the classic Mac rule for
  sticky menus; 1 s in this AppKit), not a number of mine.
- Batch `pt` at 250% is void (1 of 5): the workstation's load was 38 to 45 from other work, the
  guest took 97 s instead of 25 s to reach the session and in three runs the desktop itself gave up
  before the application had a window. Repeated as `pu` under a load of 10 to 15.
- Timing that is the emulation's, and how the check stays honest about it: a key release that
  reaches the guest more than half a second late makes the key repeat. The menu loop takes
  repeats of Up and Down only, and the check uses keys whose repetition changes nothing (Down to
  the end, Up to the start, letters, Space); it has no retries.

### Not done
- A list taller than the screen does not scroll (macOS scrolls menus and limits a combo box to
  `numberOfVisibleItems` rows). Menu item images, attributed titles, custom item views,
  alternates, indentation and tool tips of items are not drawn. The menu look (Inter 13, 22 point
  rows, 8 point corners, shadow) is upstream's and not measured against macOS: a menu cannot be
  rendered on the reference Mac without showing it.
- Keyboard focus stays with the application's window while a menu is up (keys reach the loop
  through it); a compositor that gives the grabbing popup the keyboard has not been tried.
- `NSEvent.timestamp` counts from the reference date, not from system start as on macOS.

## State
| Item | State |
|---|---|
| arm64 Cocoa system root | built from the pin (`build-cocoa-root.nix`, `build-desktop-root.nix` with our patches); every library reference and imported symbol resolves against itself plus the base image (`cocoa_survey.py`) |
| Minimal AppKit application (`hello/`) | runs on the desktop in QEMU arm64: window, label, button, text field, pointer, keys, menu key equivalent |
| Our frames around Mac windows, Mac semantics of the buttons | see Results |
| Scale 100% to 200% including fractional steps, live change | see Results |
| Upstream Terminal and Calculator on our desktop | see Results |
| Menus | a strip in each window (section "Menus"), pointer and keyboard |
| Popup windows (menus, context menus, submenus, pop-up and pull-down buttons, combo boxes, tool tips) | `xdg_popup`s with one tracking loop, pointer and keyboard; see "Popups" and "Popup results". Firefox's menus and panels: see there |
| Firefox on arm64 | official 160.0, sandboxes off, software WebRender in the main process: the browser window with its chrome, a local page with text and an image, keyboard into a page's field, pointer on a page's button (12 November, below). a page over HTTP, https://example.com. Not yet: sandbox on, GPU process, system trust store |
| Auto Layout | layout API on NSView and the controls, constraint activation, a Cassowary solver (patch 0011); three constraint layouts equal in structure to macOS |
| Control geometry and drawing | macOS 26 numbers and look, measured (patch 0012); compared control by control with `conformance/sheet.py` |

## Firefox on arm64 (official 160.0, QEMU, executed)
- Official Firefox 160.0 for macOS is universal (x86_64 + arm64), minimum macOS 11, LC_DYLD_INFO
  fixups (lazy binding), Developer ID signatures. Fetched at test time from Mozilla's CDN,
  checksum checked against Mozilla's SHA256SUMS, unpacked with 7-Zip from nixpkgs; never committed.
- Static survey (`cocoa_survey.py`, `runs/cocoa/firefox/survey-root2.md`): 27 Mach-O images, 45
  system libraries, 2413 imported symbols. Against `build-firefox-root.nix` (our root, upstream's
  small frameworks built for arm64, Security, SystemConfiguration, our Metal/MetalKit/Accelerate,
  patches 0001 to 0003): no library absent, 19 symbols missing, none of them stops XUL at load
  (the load-time gaps left are in the updater and installer helpers).
- How far it gets, same result with the default sandbox and with Mozilla's switches
  (`runs/cocoa/ff-sandbox`, `runs/cocoa/ff-nosandbox`):
  1. `firefox` starts: the Developer ID signed executable and libmozglue load and run on our
     kernel without any code-signing refusal.
  2. XUL loads (before the additions: `dlopen(XUL): image not loaded`, `runs/cocoa/f1`) and
     initialises: the process connects to the compositor (`WindowServer: scale 120/120 ...
     frames by the compositor`), AppKit starts (`firefox[64] *** Unknown color catalog Basic`).
  3. A child process is started and reaches its sandbox set-up (default sandbox run only).
  4. NSApplication runs: Firefox creates its first window, a hidden 100x100 borderless helper
     (`window ... level 2 frame {{0, 668}, {100, 100}}`), and its menu is installed
     (`NSApplication: waiting for the menu bar server`).
  5. About two minutes after start the main process dies with SIGSEGV (exit 139), before any
     browser window. No page, no input, no network. About 3.5 minutes of emulation per attempt.
     Cause found with the Homebrew stream's `libhbcrash` (`runs/cocoa/ff-crash`): a background
     thread calls `SecItemCopyMatching` and then `CFArrayGetCount` on the result (XUL+0x154841c,
     +0x1548430; fault address 0x20 in `objc_msgSend`). Our Security answered success without
     writing the result when no keychain daemon runs, so Firefox used an uninitialised
     pointer. Patch 0009 defines the result on every path and answers `errSecItemNotFound`
     for an empty search, as an empty keychain does on macOS.
  6. With 0009 (`runs/cocoa/ff-trace`): the main process lives on, its GPU helper cannot start
     (below), Firefox falls back by itself (`Fallback WR to SW-WR`, `Failed to launch GPU process
     after 3 attempts`), then aborts on a lazily bound `proc_name` (in libproc at the pin, not
     exported).
  7. With `proc_name` exported and upstream's per-user directory helper (`runs/cocoa/ff-trace2`,
     the last run): the main process runs to the end of the session (13 minutes), creates its
     browser window object (`window ... level 0 frame {{0, 767}, {1, 1}}`) and never sizes or
     shows it; nothing of Firefox is on the screen (`shots/05.png`). Its child processes die or
     hang: 289 child starts stop before `sandbox_init` with an empty `Sandbox error:` (a silent
     `return false` in Mozilla's set-up; `confstr(_CS_DARWIN_USER_CACHE_DIR)` was one cause and
     is fixed for the content process, the cause for the others is not found yet), and the
     content process that does get its sandbox is denied the compositor's socket by it (967
     `deny network-outbound .../wayland-0`).
  So today: binary loads, XUL initialises, AppKit and the menu come up, the browser window
  object exists; no window on screen, no page, no input, no network.
- The sandbox, both paths:
  - Default (sandbox on). A child's `sandbox_init_with_parameters` is refused by the profile
    compiler: at the pin `define expects a name and a value` (seen on x86_64 with 158); with
    upstream's tip compiler (patch 0007) `line 22: unbound variable 'default'` (Mozilla passes
    an operation name to its own function). Firefox prints the profile and the error; the
    child then ends (Mozilla treats a failed sandbox as fatal for that process) and the parent
    carries on. Nothing here pretends the sandbox succeeded.
  - Patch 0008 extends the compiler (operation names as values, string functions, numbers and
    comparisons, when/unless, defined?, a profile's own function as a filter; require-all,
    require-not and with-filter compiled to the strict side: what cannot be checked makes a deny
    rule broader and leaves an allow rule out). On the build host, against Mozilla's own
    sources for 160 (`security/sandbox/mac/SandboxPolicy*.h`), all six profiles now compile:
    content 1993 bytes, socket 1494, GPU 656, RDD 651, GMP 497, utility 387; with the tip's
    compiler alone none of the six did. In the guest the kernel then really enforces the
    compiled profiles on Firefox's children: the socket process (`deny file-read-data
    /etc/master.passwd`: our libc reads the password file where macOS asks a daemon) and the
    content process (`deny network-outbound /private/var/run/qemu-desktop/wayland-0`: on macOS a
    content process reaches the window server through a Mach service that Mozilla's profile
    allows; our window server connection is a Unix socket the profile knows nothing of). The
    honest fix for the second is in the system, not in Mozilla's profile: treat the profile's
    permission for `com.apple.windowserver.active` as permission for the compositor socket. Not
    built.
  - Disabled (`MOZ_DISABLE_CONTENT_SANDBOX=1`, `..._GMP_`, `..._RDD_`, `..._SOCKET_PROCESS_`,
    `..._UTILITY_`, `..._GPU_SANDBOX=1`, Mozilla's documented switches): no sandbox error; before
    patch 0009 the same crash at the same place, so that crash was not the sandbox. Not yet run
    again after 0009: it is the next run to make, since it takes the sandbox out of the way of
    the first visible window.
- 12 November, the first visible window and page (sandboxes off, `checks/firefox-window.json`,
  `firefox-page.json`; runs `ff-w2`, `ff-p3`, screenshots in their `shots/`):
  - Why nothing was shown before was not sizing. Firefox maps its window 1 by 1, hides it, sizes
    it, shows it. The WindowServer library answered a configure sent before the unmap; the
    compositor resets a toplevel on unmap and closed the connection (`xdg_wm_base error 4: wrong
    configure serial`). Now (patch 0004): configures that arrive before the compositor has
    passed an unmap (a `wl_display.sync` barrier) are dropped unanswered; a remapped toplevel
    does the empty commit and waits for its configure; a window is not mapped with a frame from
    before it was resized; a connection the sandbox refuses is final (the process is headless,
    no 60 s retry per call).
  - Compositor (worktree commit 25ec50c and the one after): a new window larger than the work
    area opens at the size that fits, frame included, and one that grows itself past it later
    is held to it (a backend capability, on for wm-darwin only); a window remapped at another
    size is placed afresh.
  - `ff-w2`: window with frame, tab strip, address bar, toolbar, about:blank. `ff-p3`: the page
    `guest/firefox-page/index.html` (heading, paragraph, a PNG, a coloured block, a field, a
    button) drawn by Gecko through WebRender's software renderer; the page reports through the
    window title: `SF page: loaded image=160x100 typed=abc clicks=1` after the driver typed
    `abc` and clicked the button; the block is 160 pixels wide for 160 CSS pixels at 100%.
  - Network (`checks/firefox-http.json`, `firefox-https.json`; runs `ff-http1`, `ff-https1`, both
    pass): the guest takes a DHCP lease on QEMU's user network (`qemu-net-up en0`); the test
    page and its image are fetched over HTTP from a server on the host's loopback (its log has
    the two GETs); `https://example.com/` loads, the window is titled "Example Domain", no
    certificate or connection error page. The chain was accepted by Firefox's own NSS roots:
    our Security answers an empty keychain, so no system trust store took part (the system
    trust path proper, trustd and enterprise roots, is not there at the pin). In the HTTPS
    screenshot, taken 30 s after the title while the page was still "transferring", the
    paragraph is drawn and the heading is not: not looked into. Both runs used the second
    compositor change: the window sits at the work area's origin, whole frame on screen.
  - Conditions: all six `MOZ_DISABLE_*_SANDBOX` switches; `guest/firefox-profile/user.js`
    (documented preferences only: `gfx.webrender.software`, `layers.gpu-process.enabled=false`,
    one content process, no first-run pages, no background traffic, and the first-run Terms of
    Use sheet switched off as Mozilla's test harnesses do - a check does not accept terms for
    anyone; whoever uses this Firefox for real starts it without the file). Firefox itself
    shows "The security sandbox is disabled" in the window.
  - Open: the GPU process (starts with sandboxes off, tries OpenGL on Mesa's software
    rasteriser, is killed by Firefox for an IPC timeout under emulation); one run of five
    (`ff-ns3`) died in the parent with `pthread_cond_wait` returning EINVAL (not reproduced,
    cause not found). In `ff-p3` the window was first mapped at 921x691 and then grew itself
    off the screen's right edge; with worktree commit 0e3967f it is held to the work area
    (`ff-http1`, `ff-https1`).
  - Sandbox on, built and run (12 November; `checks/firefox-sandbox-page.json`, `-http`, `-https`:
    the same checks with all six switches removed and `PD_SANDBOX_TRACE=1`; run `ff-sb4`):
    - A correction first. The earlier statement here that the content process reached its
      sandbox after patch 0010 was wrong. The cherry-pick had left out the line that hands the
      directory helper to libc (`.dirhelper = _dirhelper` in `pd_libSystem_init.c`), so
      `confstr(_CS_DARWIN_USER_CACHE_DIR)` failed with EIO in every process and every content
      process stopped before `sandbox_init` (the silent `Sandbox error:`; found by listing
      the children's command lines in the guest, run `ff-sb3`, and by asking confstr from the
      conformance program). 0010 now carries that line.
    - Patch 0014 (libsandbox): a profile's `(allow mach-lookup (global-name
      "com.apple.windowserver.active"))` is also emitted as an outbound rule for the one path
      `$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY`; profiles without it get nothing. Checked on the
      build host against Mozilla's six profiles (content, GPU and GMP with the window server
      parameter TRUE gain one rule; socket, RDD, utility none; none without the environment).
      With `PD_SANDBOX_TRACE` set, `sandbox_init` says which process it sandboxed or why not.
    - `ff-sb4`, sandbox on: all checks pass (page, image, keyboard, pointer), and Firefox no
      longer shows its "sandbox is disabled" bar. Four children report `sandboxed`; by the
      process list of `ff-sb3` these are the socket process, the RDD process and content
      processes (one with file access); the pairing of pid to kind is inferred, not printed.
      No child failed to start its sandbox. The content processes made no attempt on the
      compositor's socket (they are headless, as on macOS).
    - Denials that remain, all in sandboxed children, none fatal:
      `file-read-data /etc/master.passwd` (48): our libc reads the password file for
      `getpwuid`; macOS asks opendirectoryd over Mach, which the profiles allow. Ours to fix
      (a lookup service, or the profile compiler mapping that Mach permission too); macOS
      would also deny the file. `file-read-data /usr/share/fonts` (18) and
      `/etc/fonts/fonts.conf` (3): our CoreText finds fonts through fontconfig, whose paths
      Mozilla's profile does not know (it allows `/System/Library/Fonts` and the font
      services); macOS has no such files. Ours to fix in CoreText (fonts from
      `/System/Library/Fonts` without fontconfig's files in a sandboxed process).
    - Network with the sandbox on (`ff-sb5-http`, `ff-sb5-https`): both pass, four children
      sandboxed in each, none failed. One more denial there:
      `file-write-create /private/tmp/CoreText.XXXXXX` (2, HTTPS run): our CoreText writes a
      font it was given as data to a temporary file; macOS keeps it in memory and would deny
      the write too. Ours to fix in CoreText.
    - The GPU process is still switched off by preference, so its sandbox and the rule of 0014
      in a process that does use the window server are not exercised in the guest yet.
  - Fonts in Firefox (`checks/firefox-fonts.json`, page `guest/firefox-page/fonts.html`, run
    `ff-fonts1`): 36 samples (nine families at two weights, system-ui at nine weights and six
    sizes, italic, emoji, CJK, Greek/Cyrillic, symbols), each also drawn into a canvas and its
    ink counted; none is blank, so bold system text does draw (the heading missing from the
    `example.com` screenshot was not a font that cannot draw; not explained). What the
    screenshot shows instead: every family (serif, monospace, Times, Courier, Menlo, Helvetica)
    comes out as Inter, weights 100 to 500 as Regular and 600 to 900 as Bold. Firefox gets
    one family from our CoreText although the root has Nimbus Roman/Sans/Mono, Source Code Pro
    and Inter Medium/SemiBold; and the root has no CJK or emoji font at all (the test cannot
    yet tell a glyph from a "missing glyph" box). Open: CoreText's font enumeration as
    Firefox uses it, generic family mapping, CJK and emoji fonts under open licences.
  - Sandbox design as first written: on macOS Firefox's content processes have no window server
    connection at all (`HAS_WINDOW_SERVER` is FALSE in the content profile), so the content
    process must simply be headless here, which the refused-connection change gives. For
    processes whose profile does allow `com.apple.windowserver.active`, libsandbox should add
    an outbound rule for the compositor's socket path when it compiles that permission: the
    translation in the layer that knows the window server is a socket here, kernel enforcement
    unchanged. A broker passing a connected descriptor needs the parent's cooperation, which
    Firefox does not give.
- Still ahead after the crash: layer-backed rendering through QuartzCore (software only in
  QEMU; on the M1 Firefox's WebRender would use OpenGL through Mesa/AGX, there is no Metal
  device), popup windows (its menus and panels are borderless child windows), fonts, HTTPS and
  the trust store (tip work, not in the pin), the content process model.

## Patches and their state (`integrations/puredarwin/cocoa/patches`, against the pinned tree)
| Patch | What | From | At upstream's tip (rebase stream's inventory) |
|---|---|---|---|
| 0001 | AppKit: NSFontWeight constants, NSImageSymbolConfiguration | tip 71a9d0b0 (part) | already there: drop |
| 0002 | AppKit: NSSharingServicePicker | ours | applies |
| 0003 | AuthenticationServices: browser side of ASWebAuthenticationSession | ours | applies |
| 0004 | WindowServer: xdg-decoration, scale (wp_fractional_scale + wp_viewporter), size limits, toplevel states | ours | needs porting |
| 0005 | AppKit: no title bar in a compositor frame, scaled backing stores, backing-change notifications, zoomed/miniaturised from the compositor | ours | needs porting |
| 0006 | Onyx2D: glyphs rasterised at the device scale | ours | needs porting |
| 0007 | libsandbox: SBPL functions, imports, named profiles | tip 1ebd97cb (part) | already there: drop |
| 0008 | libsandbox: the rest of the profile language Firefox uses, strict-side handling of what cannot be checked | ours | not checked |
| 0009 | Security: SecItemCopyMatching defines its result, errSecItemNotFound for an empty search | ours | not checked (the file is unchanged at the tip) |
| 0010 | libSystem: per-user local, temporary and cache directories (with the init line that gives libc the helper, missing from the first cut) | tip 1ebd97cb (part) | already there: drop |
| 0014 | libsandbox: the window server Mach permission also allows the compositor's socket; `PD_SANDBOX_TRACE` | ours | not checked |
| (recipe) | libSystem: `proc_name` exported | tip 7f10e1a1 (one line) | already there: drop |
| 0011 | AppKit: layout API, constraint activation, Cassowary solver (`PDLayoutSolver`, written here from the Badros/Borning/Stuckey paper), whole-tree layout in y-down coordinates, `-layout` called on resize | ours | not checked; the tip's AppKit has none of it (it adds NSStackView only) |
| 0012 | AppKit: control geometry and drawing of macOS 26 as measured (push, checkbox, radio, pop-up, field, slider, progress, segmented); the macOS 15 look kept behind the default `PDControlLook` = `15`; a string is drawn in a rect lower than its line | ours | not checked |
| 0013 | Onyx2D: PNG encoder writes straight alpha and reads "default" byte order as RGBA (pictures of bitmap contexts had red and blue exchanged) | ours | not checked |
| 0015 | AppKit: `NSPopUpView` array, list margins, coordinates of the old list loop | ours | not checked |
| 0016 | AppKit: `discardEventsMatchingMask:beforeEvent:` removed every earlier event | ours | not checked (same code at the tip) |
| 0017 | AppKit: popups (one tracking loop, keyboard, placement by the compositor, combo box fixes) | ours | not checked |
| 0018 | WindowServer: popup anchor and kind, placement reported back, held button across an open menu's surfaces, popups presented on order-in | ours | needs porting with 0004 |
0004 also carries the unmap/remap fixes and the headless rule; 0005 the scaling of
`cacheDisplayInRect:` to its bitmap (was 1x in a corner of a 2x bitmap).
Tip variants of 0004 to 0006 are not written yet (to go in `patches-tip/` when the rebase
stream has a tip build).

## Implementations added that answer "nothing here" (complete list)
| Symbol | What it does |
|---|---|
| `MTLCreateSystemDefaultDevice`, `MTLCopyAllDevices`, `MTLCopyAllDevicesWithObserver` | nil / empty array / empty array and nil observer: there is no Metal device |
| `MTLCaptureManager` | `supportsDestination:` NO, `startCaptureWithDescriptor:error:` NO with MTLCaptureErrorNotSupported, no scopes |
| MetalKit.framework | a library with no symbols: Firefox's libmozinference names it as a dependency and imports nothing from it |
| `ASWebAuthenticationSessionWebBrowserSessionManager` | keeps the handler; `wasLaunchedByAuthenticationServices` NO; nothing starts sessions on this system |
| `NSSharingServicePicker` | menu of the services the delegate proposes (the system has none); with none, shows nothing and tells the delegate nil was chosen |
| `xdg_toplevel` configure_bounds and wm_capabilities in the WindowServer | received and ignored |
Not stubs: the Metal descriptor classes, `ASWebAuthenticationSessionRequest`, the ten vDSP
routines, the sandbox compiler additions (rules it cannot express are made stricter, never
reported as applied when they are not), `SecItemCopyMatching` (reports an empty keychain).

## Missing, in the order it blocks things
1. Firefox: why most child processes stop before `sandbox_init`; the compositor socket for
   sandboxed content processes; then the first visible window (run with Mozilla's sandbox
   switches after patch 0009 to separate the two), layer-backed rendering, HTTPS and trust.
2. Popup windows (menus, pop-up buttons, tool tips, Firefox's panels): xdg_popup in the
   WindowServer and in the compositor, which refuses popups today.
3. Menus: the owner's choice between the three proposals above.
4. Auto Layout beyond patch 0011: layout guides, NSStackView (tip only), constraints from nibs
   exercised; the font tracking table and the missing weights; the rest of the control look
   (see the second conformance pass).
5. Scale: layer-backed content, NSOpenGLView and application-set pointer images are 1x;
   offscreen caches render at 1x.
6. For Chromium applications (Codex, Claude): remote layers across processes in our
   compositor (subsurfaces, `pd_surface_host`), about 50 libraries of missing imports that
   must all resolve at load, a GPU path without Metal.
7. Not conveyed to our frame: document-edited state, absent close button.
8. Seen, not investigated: upstream Terminal's text at about half the cell size.

## Plan for the M1
Nothing here has run on the M1. The pieces are arm64 binaries built for the same userland, so
the first M1 boot is: the root of `build-desktop-root.nix` and the libSystem of
`build-libsystem-cocoa.nix` in the RAM root, the compositor from branch `wm-darwin-cocoa`,
`cocoa_check.sh`'s hello and apps checks by hand at 200% on the panel (2560x1600, so a
1280x800 point desk; QEMU could only show 512x384 points at 200%). To look for there and not
visible in QEMU: glyph and frame sharpness on the real panel, redraw speed of AppKit's
software path at 2560x1600, the GL compositor path with shm clients that carry a viewport,
trackpad scrolling into AppKit (scroll events were not tested here at all), the SPI keyboard's
modifier keys as key equivalents. Mesa/AGX OpenGL for Firefox's WebRender comes after Firefox
shows a window in QEMU.

## How to run
```sh
integrations/puredarwin/cocoa/cocoa_check.sh hello --scale 2 --theme graphite    # one check, exit 0 = pass
integrations/puredarwin/cocoa/cocoa_check.sh frame --scale 2
integrations/puredarwin/cocoa/cocoa_check.sh live-scale
integrations/puredarwin/cocoa/cocoa_check.sh apps --scale 1.5 --theme amber-phosphor
```
`cocoa_check.sh` builds the root (`build-desktop-root.nix`) and the Homebrew stream's libSystem
(`../homebrew/build-libsystem.nix`: the `___chkstk_darwin` fix), makes the image
(`cocoa_image.py`: the QEMU stream's template with the root grown to 3 GiB) and runs the steps
of `checks/<name>.json` (`cocoa_run.py`: QMP pointer and keys, console markers, measurements
from the framebuffer). The compositor is `runs/cocoa/target/...` when built from the worktree
(as `tools/qemu_desktop.sh --src` builds it), else the M1's current binary.

## What should go into the default images
- Patches 0004 to 0006 with the root of `build-desktop-root.nix` (about 180 MB: frameworks,
  fonts, Mesa, LLVM for Mesa's software rasteriser, Swift runtime for Foundation's compat
  framework).
- Compositor commits a447cef and 06d0ea9 (branch `wm-darwin-cocoa`): toplevel states for
  maximise/minimise; a test option for the autopilot's scale phase.
- The default libSystem (`../libsystem-default`); for Firefox additionally patches 0007, 0008,
  0010 and the `proc_name` export (`build-libsystem-cocoa.nix`), and patch 0009 in Security.

## Results (QEMU arm64, 1024x768 framebuffer; every row executed, run directory under `runs/cocoa/`)
Checks are `cocoa_check.sh <check>`; "px/pt" is measured on the framebuffer from a marker the
application draws; "ink" is the pixel height of the first two text lines.

| Check | Scale, theme | Result | Measured |
|---|---|---|---|
| hello (frame, button, pointer, keys, key equivalent) | 100%, nextstep-classic | pass 9/9 | 1.000 px/pt, ink 14/13 |
| hello | 125%, jade-lacquer | pass 9/9 | 1.250 px/pt, ink 15/15 |
| hello | 150%, amber-phosphor | pass 9/9 | 1.500 px/pt, ink 19/19 |
| hello | 200%, graphite | pass 9/9 | 2.000 px/pt, ink 25/24 |
| frame: our close button asks the application (refused once, then allowed, application ends) | 200% graphite; 100% nextstep-classic | pass 8/8 both | |
| miniaturize: our button -> `windowDidMiniaturize:`, `isMiniaturized` | 100% | pass | |
| zoom: Ctrl+Shift double click on our title bar -> resize and `isZoomed` | 200% graphite | works when the double click registers: seen in two runs (`zoom-evidence-s2-graphite`, `zoom-s2-graphite`: 510x351, `zoom-state zoomed=1`). Under emulation the compositor's 400 ms double-click window is met about one time in five, so the check itself is unreliable; restoring by a second double click has not been observed | |
| apps: upstream Terminal (a typed command ran in its shell) and Calculator (the + key lights when clicked), both in our frames | 100% nextstep-classic, 150% teal-blueprint, 200% graphite | pass 9/9 each | Calculator 1.01 / 1.52 / 2.03 px/pt |
| live scale change (the desktop steps 100 -> 125 -> 150 -> 175 -> 200 -> 250 -> 300 -> 100%, 25 s each) | teal-blueprint | pass 10/11 (`live-scale-teal-blueprint`): `windowDidChangeBackingProperties:` with 1.250, 1.500, 1.750, 2.000, 2.500, 3.000, 1.000 in that order, a screenshot at each. The one failed check: the compositor counted 3 commits at a stale scale over the 7 changes (a frame already under way when the scale changed; at 250% one 2.0 buffer was shown resampled once). Two faults were found and fixed to get here: the notification was skipped when a resize had rebuilt the backing first, and `NSWindow.backingScaleFactor` read 1 while AppKit's frame lay off the screen in points | |
| no resampled commit at a fixed scale | all of the above | pass | |

Two faults of the test tooling were found and fixed, and the affected runs repeated: two
checks had matched their own echoed command line on the console (false passes of the metrics
run and of the Terminal step); and the image cache was keyed by file sizes and times only, so
a rebuilt framework of the same size was taken for the old one (one live-scale rerun ran the
old AppKit). The table has only results from after both fixes where they matter.

Seen and not fixed: upstream Terminal draws its text at about half the cell size at every scale
(also at 100%, so not a scaling fault; not investigated); an application-set pointer image stays
at 1x; windows are placed by the compositor, the position AppKit asked for is ignored.

### 14 November: the checks again, on patches 0001 to 0033 (QEMU arm64, pin root)
Run by the Terminal.app stream after its changes (the menu strip, bindings, toolbars, scrollers,
the remembered string sizes); the Foundation of these runs is the pin's, not the patched one of
the M1.

| Check | Scale | Result | Run |
|---|---|---|---|
| hello | 200% | pass (27 steps); its window carries the menu strip "CocoaHello File" | `reg2-hello-s2` |
| frame | 100% | pass (29 steps) after the check's click on our close button was moved up by the strip's 22 points (`checks/frame.json`, also `zoom.json`, `miniaturize.json`: they place the title bar relative to the application's view). With the old offset it failed: the click landed on the strip | `reg4-frame-s1` (`reg2-frame-s1`: the failure) |
| apps (upstream's Terminal and Calculator) | 150% | pass (29 steps) | `reg4-apps-s1.5` |
| popups | 200% | pass (269 steps) | `reg4-popups-s2` |
| miniaturize | 100% | pass (10 steps) | `reg4-miniaturize-s1` |
| scorecard | 100% | 2077 compared, 1214 equal within 0.01, 836 different, 27 missing (`scorecard-007`, 12 November: 1214, 834, 29). Changed: the two scroller widths are answered now (15 and 0 where macOS has 17 and 17); `layout/centered` has one more equal value; `renderScale` is 1 because this run was at 100% (the earlier one at 200%) | `reg4-metrics2`, `runs/cocoa/conformance/scorecard-008.md` |

Not run: `live-scale`, `zoom`, and every Firefox check (`build-firefox-root.nix` was not built
with 0019 to 0033 for a run; the Terminal.app tree takes ApplicationServices and ImageIO from it,
so it builds). Firefox on the M1 was not tried. Trap met: `cocoa_check.sh metrics` without
`--tree <build-conformance.nix output>=/` passes with an empty result.

## Conformance against real macOS (first scorecard)
Reference: macOS 26.5.2 on the bench controller, used only as a renderer in `~/sf-cocoa-ref/`
over ssh (no GUI session touched: an NSWindow that is never shown and `cacheDisplayInRect:`
draw at 2x headless). `conformance/metrics.m` builds unchanged on both sides;
`conformance/compare.py` compares the JSON. Raw files: `runs/cocoa/ref-macos/`,
`runs/cocoa/conformance/` (`scorecard-001.md`, `side-by-side-001.png`, `diff-001.png`).
No Apple font or artwork was copied anywhere; the reference PNGs stay under `runs/`.

1548 values compared: 382 equal within 0.01 pt, 867 different, 299 missing on our side.

| Area | Finding |
|---|---|
| System font | upstream ships Inter (Regular and Bold only) as the system font. Against macOS's system font: ascender within 0.03 pt at every size; descender 0.3 to 0.8 pt deeper; cap height +3.3%, x-height +3.7%; default line height equal at 9, 10, 12, 13, 14, 17 pt and 1 pt taller at 11, 15, 20, 24, 28 pt |
| String advances | Inter has no size-dependent tracking: a 37-character label is 4.1% narrow at 9 pt, 1.2% wide at 13 pt (+2.6 pt), 5.9% wide at 17 pt, 12% wide at 28 pt (+49 pt) |
| Tracking table (computed, not yet applied) | least-squares tracking per size that brings Inter to the reference, in 1/1000 em: 9: +15.4, 10: +8.6, 11: +2.8, 12: -3.1, 13: -9.0, 14: -13.8, 15: -18.7, 17: -28.5, 20: -40.1, 24: -50.6, 28: -51.1. Predicted residual on the text strings after it: within 2 pt up to 15 pt size, within 4 pt at 28 pt; digits stay 1.5 to 14 pt narrow and capitals wide, because the glyph proportions differ. Advances cannot be made identical without Apple's fonts |
| Weights | ultraLight, thin, light resolve to Inter-Regular; medium, semibold, heavy, black to Inter-Bold (macOS has nine distinct weights) |
| Monospaced | Nimbus Mono PS where macOS has Menlo / SF Mono; `monospacedDigitSystemFontOfSize:weight:` missing |
| Control sizes (regular) | push button 92x32 (macOS 80x24), checkbox 113x18 (110x16), pop-up 106x22 (119x24), text field height 22 (24), segmented 136 (148) wide, slider `sizeToFit` returns 10000x10000 |
| Layout API | `intrinsicContentSize`, `fittingSize`, `alignmentRectInsets`, `firstBaselineOffsetFromTop`, `baselineOffsetFromBottom` do not exist on NSView (270 of the 299 missing values): Auto Layout based applications cannot lay out against this AppKit |
| Colours | 10 of 20 system colours equal; different: disabledControlTextColor, controlColor, placeholderTextColor, controlTextColor, keyboardFocusIndicatorColor, windowBackgroundColor and four more |
| Window | titled frame for 400x300 content: macOS 400x332; ours 400x300 here because the compositor draws the frame (by design) |
| Pixels (one view tree, inactive window, 1x) | 6.7% of the pixels differ by more than 8%; structural dissimilarity 0.10. Visible in `side-by-side-001.png`: older control artwork (bordered capsule button, square checkbox, square field), slider knob drawn at 0 for a value of 0.4, the offscreen render on our side is at 1x (`renderScale` 1 against 2) |

Order of work this gives: layout API on NSView (blocks every modern application), control
geometry to the reference, the tracking table and the missing weights (Inter has all nine
as static or variable fonts under the SIL Open Font License), then control drawing. The
conformance suite so far is this one program; the areas the owner listed (tables, menus,
toolbars, sheets, Auto Layout, dark appearance, ...) are not written yet.

### Second pass (12 November): layout, control geometry, control look
- Layout. At the pin and at the tip alike NSLayoutConstraint was data, AppKit relaxed required
  equalities among a view's direct subviews, `setActive:` set a flag only (nothing made with
  anchors was laid out), and "top + 20" went up in unflipped views. Patch 0011 replaces that.
  Three constraint layouts are in the suite (`layout` in the JSON); against macOS their
  structure is equal (label at x 18 on both, a field stretched 20..300 on both, fitting sizes
  from the same sums), what differs is the width of text in Inter.
- Values: 2077 compared, 1214 equal within 0.01 (first scorecard: 382 of 1548), 29 missing
  (299). `runs/cocoa/conformance/scorecard-007.md`.
- Control sheet: `metrics <path>` also renders every control at the three control sizes, once
  in an ordinary unshown window (inactive look) and once in a window that says it is key
  (active look, still never shown); `conformance/sheet.py` cuts each control out of both
  pictures by its own frame and compares. 45 controls on each sheet
  (`sheet-sheetActive-003.md`/`.png`, `sheet-sheet-003.md`/`.png`): 22 frames the same size and
  the rest within 2 points (Inter's widths); mean difference 9.9 of 255 (active) and 10.0
  (inactive); 7.7% and 7.6% of pixels off by more than 24 (the run before the slider and
  accent fixes: 14.4%). Sliders 0.4% off, progress bars and disabled buttons a few percent;
  the worst are the labels and titles (14 to 17%), which is the glyphs: Inter against the
  system font, drawn about a point lower. Also left: the field's ring (macOS draws it one
  point outside the frame; a cell here cannot), the slider knob's halo outside the frame
  (same reason), heavier pop-up arrows.
- Two looks: the macOS 26 look is the default; `PDControlLook` = `15` (or `PD_CONTROL_LOOK=15`)
  gives upstream's macOS 15 look in the same geometry. Dark appearance and pressed states of
  the 26 look are not measured (noted in `PDControlDrawing.m`).
- Not yet: tracking table, missing weights, dark appearance, tables, menus, toolbars, sheets.

## Claude and Codex desktop applications (static survey)
- Codex: OpenAI's documented download (`persistent.oaistatic.com/codex-app-prod/Codex.dmg`,
  896 MB, sha256 488009cf...ad4423a) contains `ChatGPT.app` (bundle id com.openai.codex,
  26.1106.51428, macOS 13.0 or later, arm64 only): OpenAI's own Chromium runtime ("owl",
  Chromium 157.0.8103.51, `app.asar`), Sparkle, a bundled Node 24.22.1 and Codex CLI, helper
  applications, App Sandbox entitlements on every executable. Survey
  (`runs/cocoa/apps/codex/survey.md`): 91 Mach-O images, 119 system libraries, 4670 imported
  symbols, 817 missing, 48 libraries absent. The runtime uses chained fixups, so every import
  binds at load; it alone has gaps in 49 libraries.
- Claude: not obtained. Anthropic's documented download links answer 403 to an automated
  client; a person has to fetch `Claude.dmg` with a browser into `runs/cocoa/apps/claude/`.
- Beyond linking, Chromium needs: remote CoreAnimation layers into the window (upstream's
  `pd_surface_host_v1`; our compositor has neither that nor subsurface compositing: missing),
  the sandbox (kernel extension present; newer profile compiler carried as patch 0007;
  `--no-sandbox` is Chromium's switch), a GPU path without Metal (ANGLE on OpenGL through
  Mesa, or SwiftShader), IOSurface (framework and a launchd job for upstream's iosurfaced are in the root; untested),
  Mach bootstrap/XPC (present, untested), Keychain and trust (Security present, trustd
  missing at the pin), LaunchServices URL schemes (untested), audio (lab coreaudiod; AVFAudio
  absent), accessibility and screen capture (missing).
- Order: Firefox 160 first, then an official Electron release's blank window as the Chromium
  stepping stone, then Codex and Claude. The command-line tools (Claude Code, Codex CLI) are
  a separate stream's work and the short route.
