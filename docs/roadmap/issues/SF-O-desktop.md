## Goal
A desktop a person can read and set up from the desktop itself on the 13-inch M1 MacBook Pro (J293: 2560x1600 at about 227 dpi). The owner's words after first use: "We need desktop scaling that is holistic and works well. We need chonkstep menu support for changing themes, as well as the system scaling; the right click menu is a good place to land things like that. We can add WiFi control to the desktop menu, which network is connected / WiFi on off, sound settings etc, but some very high quality desktop scaling is imperative as it's hard to read the terminals." (GitHub issue #38.)

## Decision
**Scaling is done the way Chonkstep already does it on Linux: the desk stays in panel pixels and everything is *made* at the scale. Nothing is drawn small and stretched.**
- Upstream Chonkstep has a UI scale: the theme is kept at 100% and multiplied (`Theme::scaled`), the decoration engine is built at the scale (`RasterThemeEngine::with_fonts_at_scale`), the shell's own chrome is drawn at it (`UiChrome`), and `Shell::apply_session_state` swaps all of that in place. The standalone (Darwin) profile refused every such change; it now goes through the same applier (`Shell::set_standalone_look`). No second scaling design was written.
- The compositor (`wm-darwin`) keeps its scene, the window manager and both renderers (CPU, GL) in panel pixels at every scale, so the renderers did not change and a frame is compared against the CPU render exactly as before. The scale decides how large things are made: the shell rasterises at it, the pointer arrow is drawn at it, and clients are told it and render their own buffers at the panel's density.
- **Steps: 100, 125, 150, 175, 200, 250, 300 percent** (in 120ths, the unit of `wp_fractional_scale_v1`, so both sides round alike). Fractional steps are worth having on this panel: 200% gives a 1280x800 desk, 150% a 1707x1067 one. They stay sharp because nothing is resampled: the shell's fonts and metrics are rasterised at the fractional size, and the terminal renders its glyphs at `18 px x scale` and sends a buffer of exactly `round(logical x scale)` pixels through `wp_viewporter`.
- **Default without configuration: by the panel**, the step nearest `width / 1280` and never under 100%: 200% on the J293. `CHONKSTEP_SCALE` overrides it, as on every other Chonkstep session; a choice made from the menu is persisted and comes between the two.
- **Windows keep their logical size and place across a change.** A client that chooses its own size (the terminal) keeps choosing: it is told the new scale, redraws, and the window follows the buffer it commits, so a terminal keeps its columns and rows at every scale. A client is asked for a size only when its window does not fit the panel at the new scale, and to get back the size it had before that.
- **The settings live in the desktop (right-click) menu**, built from the menu Chonkstep already has (`CascadeMenu`, the Theme rows of the Linux root menu): `Terminal`, `Theme` (the built-in themes, the current one marked), `Display` (the steps), `Wi-Fi`, `Sound`, `End Session`. Command-Space opens it from the keyboard; arrows, Return and Escape work in it.
- Wi-Fi and sound are read and changed from inside the compositor without starting anything: the interface's 802.11 requests (`SIOCGA80211`/`SIOCSA80211`: power, state, network name, signal), the ones `wifiutil` and `pd-netd` use, and coreaudiod's Mach service, as the volume keys already do.

## What a client is told, and what it may send
| Mechanism | Value | For |
|---|---|---|
| `wl_output.scale` | the next whole number (125% to 200%: 2; 250%, 300%: 3) | clients that know only whole scales |
| `wl_surface.preferred_buffer_scale` (`wl_compositor` version 6) | the same | the same, per surface |
| `wp_fractional_scale_v1.preferred_scale` | the scale in 120ths (150% is 180) | clients that render at the exact scale, with `wp_viewporter` |
| `xdg_toplevel.configure`, size hints, window geometry, input region, pointer positions | logical units (panel pixels over the scale) | everything |

- A buffer at the panel's density (`buffer_scale n` on a whole step, or viewport destination with a buffer of `round(logical x scale)`, within a pixel) goes to the frame untouched, with its damage.
- Any other SHM buffer is resampled to the size its surface has on the panel: repeated pixel for pixel on whole multiples, filtered otherwise. This is the only place anything is ever stretched, and only for a client that did not render at the scale; `WMSCALE resampled` says so once and `resampled_commits` counts it.
- A dma-buf is never resampled (it is GPU memory, shown without a copy): one that is not at the display scale is shown pixel for pixel, small, and its window is measured in panel pixels (`WMSCALE unscaled`). The GL test clients are the only ones today.
- Still refused: buffer transforms, attach offsets, viewport source rectangles.

## Pointer, trackpad and scroll (the owner's question: do they need to change with the scale?)
**No, and they do not.** What the code did before, what it does now, and the test:

| Item | Before | Now | Test |
|---|---|---|---|
| Pointer speed | Relative counts from the HID queue are added to a position kept in panel pixels (floating point); there is no acceleration in the compositor (any is in the kernel's trackpad driver, before the scale exists). | Unchanged: the pointer still lives in panel pixels, so the same finger travel moves it the same distance on the glass at every scale. Only what clients are told is divided by the scale (`pointer_for_clients`), with the fraction kept (wl_fixed, 1/256). | `tests/wayland_scale.rs` `the_pointer_covers_the_same_panel_distance_...`: 46 relative records (single counts, diagonals, fast strokes, a reversal) from the same place end on the same panel pixel at 100, 125, 150, 200 and 300 percent, exactly. Autopilot `scale`: 20 records of (5, 3) move the pointer 100, 60 at every step. |
| Scroll | One wheel step is `wl_pointer.axis` 10 units and one `value120` detent; the terminal turns 10 units into 3 lines. | Unchanged at every scale (axis values are logical and are not scaled). | Same test: the axis events of one step are identical at all five scales. Terminal host tests: two detents give `scrollback_offset=6` at 100, 150, 250 and 300 percent. Autopilot `scrollback`: ten steps are 30 lines. |
| Thresholds on the trackpad surface (tap, click, palm rejection, gestures) | In the kernel's trackpad driver; the compositor sees buttons and counts. | Unchanged: nothing there knows the scale. | By construction (the compositor has no such threshold). |
| Thresholds on screen | Double-click distance 4 px times the decoration scale (wm-core); resize handles and title bar buttons from the theme; edge snapping 10 px; a miniwindow's click-or-drag slack 6 px. | All in logical units: double-click 4, snapping 10, miniwindow slack 6, handles and buttons from the scaled theme, so each keeps its size on the glass. | `the_desk_is_drawn_at_every_scale...`: snapping is `10 x scale` at every step, also after a theme change; menu rows are `row height x scale` apart and each is hit at its centre. wm-theme's own `resize_corner_hitbox_grows_with_scale`. |
| Pointer image | A fixed 12x18 pixel arrow. A client cursor at `buffer_scale` other than 1 was a protocol error. | The arrow is drawn (not stretched) at the scale, tip at (0, 0) at every scale. A client cursor is shown as it is when drawn at the display's density, resampled otherwise; its hotspot is logical and is multiplied. | `scale::tests::the_arrow_is_drawn_at_the_scale_with_its_tip_at_the_origin` (size, tip, 16 logical rows of ink, at most 4 blended pixels on a row at every step). Autopilot `scale`: `pointer_image` is `12x18 x scale` at each step. |

## The desktop menu
| Row | What it shows | What a pick does |
|---|---|---|
| Terminal | | Opens a terminal (as before). |
| Theme | The built-in themes (16 in this build), the current one marked. | Redresses the desk in place: frames, menus, notices. Persisted. |
| Display | 100% ... 300%, the current one marked. | Changes the display scale live. Persisted. |
| Wi-Fi | "Wi-Fi is on/off", "Connected to <name>" or "Joining a network..." or "Not connected", "Signal: Excellent/Good/Weak (<n> dBm)"; "No Wi-Fi hardware" without an interface. | "Turn Wi-Fi Off/On": the radio. Joining is the guest's job: `darwin-desktop-scale.in` joins the remembered network again when the radio comes back on. Joining another network from the menu is not done (it needs a way to type a passphrase). |
| Sound | "Output: <device name>", "Volume <n>%", five levels (0, 25, 50, 75, 100, the nearest marked), Mute/Unmute; "No sound output" without coreaudiod. | Sets the level or the mute of the default output device. The volume keys (F10 to F12, and on the Touch Bar) still step in sixteenths. |
| End Session | | Ends the session (as before). |

Each change is announced in a notice ("Display 150%", "Theme: Graphite", "Volume 75%", "Wi-Fi off") and a `WMSET` line on the console.

## How to run it
- Unattended proof: `integrations/puredarwin/m1-desktop-scale/tools/ds_desktop_boot.sh <dsNN>` (see the README there for `DS_*`).
- For a person: `DS_AUTOPILOT=0 DS_SECONDS=<n> .../ds_desktop_boot.sh <dsNN> <limit>`, or `tools/gpu_desktop.sh` with the binaries below.
- Binaries: compositor `runs/m1-desktop-010/wm-darwin-scale-desktop-build-NNN/target/aarch64-apple-darwin/release/chonkstep-darwin-desktop`, terminal `runs/m1-desktop-010/terminal-application-build-006/target/aarch64-apple-darwin/release/chonk-terminal-darwin`. Both are needed: an older terminal on the new compositor is enlarged (readable, blocky at 200%, soft at fractional steps); the new terminal on an older compositor behaves as before.
- Host tests (always with `XDG_STATE_HOME` at a scratch directory): `cargo test --offline --locked -p wm-darwin --features desktop-shell -p chonk-shell -p wm-theme` in the worktree; `cargo test --offline --locked` in the terminal's directory. `CHONKSTEP_DARWIN_TEST_GL=1` runs the wire tests on the GL renderer off screen. `CHONK_SCALE_PROOF_DIR=<dir>` keeps the pictures of the desk and the terminal at every scale.

## Status
**Scaling and the menu work on the M1 and pass unattended (boot `ds05`, November 11). Nobody has looked at the panel yet.**

Builds: compositor `runs/m1-desktop-010/wm-darwin-scale-desktop-build-006` (Chonkstep branch `wm-darwin-scale` at `0e2f09b`, local only), terminal `runs/m1-desktop-010/terminal-application-build-008`.

### Proven on the machine
Boot `ds05` (GL renderer on KMS, 2560x1600; `WMAUTO done result=ok` for all 20 phases; back in verified Linux, one reset). `ds04` showed the same for the scale and settings phases with the builds before.

| What | Evidence |
|---|---|
| Default scale | `WMSCALE initial scale=200% scale120=240 source=automatic panel=2560x1600 logical=1280x800 wl_output_scale=2`, nothing set in the environment. |
| Terminals honour it | Both start 80x24 with `scale120=240 cell=22x44 buffer=1792x1088 logical=896x544` (18 px font rasterised at 36 px); `resampled_commits=0` for the whole session: no client buffer was ever stretched. |
| Everything that worked at 100% works at 200% | The existing phases pass unchanged: drag, typing, `vi` on the alternate screen, scrollback (30 lines for 10 wheel steps), copy and paste between terminals, brightness, volume keys, Touch Bar, idle blanking, resize, re-drag, minimize, menu, chords, terminal from the menu, the limit notice. Pointer sweep: 59.9 fps, 0 missed refreshes. |
| Live scale changes | `scale` phase: 100, 125, 150, 175, 200, 250, 300 percent and back to 200, about 1.8 s each, no restart. At every step: `wl_output.scale` as told (1, 2, 2, 2, 2, 3, 3), pointer image `12x18 x scale`, title bar `23 px x scale` (23, 29, 35, 40, 46, 58, 69), the window as large as its client's buffer and equal to its logical size times the scale, both terminals reporting the step's scale and cell (11x22, 14x28, 16x33, 19x39, 22x44, 27x55, 32x66) and a buffer that is their grid; 0 faults. |
| The grid survives | The terminal nobody resized is 80x24 at every step up to 250%. At 300% an 80-column terminal is wider than the panel (2608 px): both are asked to fit and become 78x23; back at 200% they are exactly what they were (82x25 for the resized one, 80x24 for the other): `round_trip_same_grid_and_logical_size=true`. |
| Renderer at every scale | The GL frame read back equals the CPU render of the same scene at each step (`differing_pixels=0`), one checksum per scale (`frame_hash`); 0 missed refreshes in the phase. |
| Pointer | 20 relative records of (5, 3) move the pointer exactly 100, 60 panel pixels at each of the eight steps; the position told to the client under it is the panel position over the scale (e.g. `453.6, 260.8` logical for `1134, 652` at 250%). |
| The menu, by synthetic right-click and clicks | Theme to Graphite (`WMSET theme=graphite`, notice "Theme: Graphite"); Display 150% and back to 200% (scale read back, notice); Wi-Fi rows against the interface ("Wi-Fi is on", connected, "Signal: Excellent (-40 dBm)"), radio off (`wifi_power=0 now_power=0`) and on again; Sound: "Output: Headphones", level to 75%, mute, unmute, each read back from coreaudiod. 10 of 10 picks. |
| The menu from the keyboard | One of the ten: Command-Space, Down, Right, Return picks the first theme again. |
| Wi-Fi comes back | After "Turn Wi-Fi On" the interface is associated again a few seconds later without help (`DSDESK_WIFI_AFTER power=on state=running joined=1`). |
| Persistence | The first session leaves `scale=240 theme=nextstep-classic` in the state directory (its own writes). A second session with the files set to 150% and Graphite and nothing in the environment starts with `source=persisted ... theme_source=persisted`, terminals at `scale120=180 cell=16x33`, 80x24. (The state directory is on the RAM root: it lasts until power-off.) |
| Buffers off the RAM root | Every terminal reports `shm=posix`; the root's free space is 45.9 MiB before and 45.6 MiB after the session. |

### Proven on the host
| What | Test | Numbers |
|---|---|---|
| The desk is drawn at the scale, not stretched | `wm-darwin` `the_desk_is_drawn_at_every_scale_and_never_stretched` (2560x1600 scene per step, pictures in `runs/m1-desktop-010/wm-darwin-scale-build-001/scale-proof/`) | Menu row pitch 20, 25, 30, 35, 40, 50, 60 px; every row hit at its centre; notice text height 9, 11, 13, 15, 18, 22, 26 px against 9 x scale; share of blended pixels never above the 100% picture's, while the 100% picture stretched to the same size has 3 to 36 times more; 17 to 19% of the pixels differ from the stretched picture. |
| The terminal's glyphs are rasterised at the scale | terminal `text_is_rasterised_at_device_pixels_at_every_scale_not_enlarged` (`terminal-application-build-006/scale-proof/sharpness.txt`) | 'H' is 12, 16, 19, 22, 25, 31, 38 rows high (cap height x font px within 0.6); grey per outline pixel 0.58 to 0.66 at every scale, against up to 1.85 (nearest) and 3.0 (bilinear) for an enlarged 100% render. |
| What clients are told, and exact buffers | `tests/wayland_scale.rs` (a real client over a socket) | At each of 14 steps (up through the scales and down again, live): fractional scale, `wl_output.scale` and preferred buffer scale agree; a `round(logical x scale)` buffer reaches the frame pixel for pixel (a four-colour single-pixel pattern compared exactly); whole-scale buffers exact on 100/200/300%, resampled in between; a client that ignores the scale is doubled pixel for pixel at 200%. |
| Pointer, scroll, thresholds | see the table above | |
| Shrink and restore | `a_window_too_large_for_the_panel_...` | 500x300 logical on a 1200x800 panel: asked to be smaller at 250% and 300%, 500x300 again at 200% and 100%. |
| GL renderer | the wire tests and the autopilot with `CHONKSTEP_DARWIN_TEST_GL=1` | GL readback equals the CPU render at every scale (0 differing pixels). |
| Menu model | `settings::tests`, `scale::tests`, chonk-shell's own | Rows for every radio and mixer state; ids round-trip; steps parse exactly. |

Totals: wm-darwin 127 unit + 19 wire tests, chonk-shell 301, wm-theme 134, terminal 75; all pass.

### Boots
| Boot | Builds | Result |
|---|---|---|
| `ds01` | compositor 001, terminal 006 | Everything but one criterion: the grid drifted by a column over a round trip (each change re-asked the window for its rounded logical size). Fixed: a client is asked for a size only when the panel forces it. |
| `ds02`, `ds03` | compositor 003, 004, terminal 006 | Both terminals exited (status 1) at the change to 300%, with the windows gone for the rest of the script. The terminal's buffers were files on the RAM root, 16 to 18 MiB each at that scale next to the previous ones. The exact errno was not captured. |
| `ds04` | compositor 005, terminal 007 (buffers in POSIX shared memory) | Scale and settings phases pass, terminals survive every scale. One failure: after the resize drag the frame was 1844x1139 around a 1836x1132 buffer (a configure that changed nothing was acknowledged without a commit; a race that was there before). Fixed on both sides. |
| `ds05` | compositor 006, terminal 008 | All 20 phases pass, second session from persisted state. |

Every boot ended `DARWIN_COMMAND_LINE_OBSERVED_LINUX_VERIFIED` with one reset.

### Not proven, or not done
- **What only a person can judge:** whether text is comfortable to read at 200% (and which step the owner prefers), the look of the chrome and menus at each scale and in each theme, whether fractional steps look as sharp on the glass as their pixels say, the feel of pointer speed (it is the speed it had at 100%: the same travel on the glass), how a live change looks while it happens (windows show their last picture stretched for a moment until the client redraws), and whether "Output: Headphones" is right for what is plugged in.
- **The cause of the terminal exits in `ds02`/`ds03`** is inferred, not read: file-backed buffers died twice, shared-memory ones did not in two boots, and the free space fits the sizes. A terminal that cannot allocate still exits and its window disappears; it now records why (`result.json`, and `WMCLIENT disconnected` on the console).
- **Memory:** buffers are now RAM (7.4 MiB per default terminal at 200%, 15 MiB for a full-screen one). Eight large terminals at 300% were not tried.
- **Clients other than the terminal:** GL (dma-buf) clients that ignore the scale are shown small, pixel for pixel; there is no such client that renders at the scale yet. Subsurfaces and popups are still not implemented in this compositor (unchanged).
- **Joining another Wi-Fi network** from the menu, a list of networks in range, and a volume slider: not done. The rows show state and switch the radio; the level has five steps.
- **Persistence across power-off:** the state directory is on the RAM root. The wrapper can start a session at a chosen scale and theme (`DS_SCALE`, `DS_THEME`); a persistent home is another issue.
- **`CHONKSTEP_SCALE` / `CHONKSTEP_THEME` from the environment** are covered by host tests only (the boots used the automatic and the persisted paths).
- **The interactive mode of the wrapper** (`DS_AUTOPILOT=0`) has not been booted.
- `hid::tests::open_flags_character_rejection_and_nofollow_do_not_use_hid_nodes` failed once in a parallel run and passes alone (it checks that a descriptor number is closed while other test threads open files). It was there before and was not touched.

### For the person at the machine
1. The desk comes up with two terminals. Read a line of text in one at arm's length. Is it comfortable?
2. Right-click the desk (two fingers, or Control-click), or press Command-Space. The menu has Terminal, Theme, Display, Wi-Fi, Sound, End Session.
3. Display: pick 150%, then 125%, then 250%, then back to 200%. Each time everything should change together within a second or two: text in the terminals, title bars, the menu itself, the pointer. Text should be sharp at every step, never smeared. A terminal should keep its 80 columns (at 300% it is narrowed to fit the screen and comes back when you go down again).
4. Move the pointer across the screen at 100% and at 300%: the same swipe should carry it the same distance on the glass. Scroll a long listing (`seq 1 300`) with two fingers at two scales: the same swipe should scroll the same number of lines.
5. Drag a window by its title bar, resize it by the bottom-right corner, miniaturize it and click the miniwindow, at 150% and at 250%: the grips should be where they are drawn.
6. Theme: pick two or three. Frames, menus and notices change at once; terminals keep their own colours.
7. Wi-Fi: the submenu should name your network and a signal. Turn Wi-Fi off, open the menu again ("Wi-Fi is off"), turn it on, wait ten seconds, open it again: it should be connected.
8. Sound: play something (`pdtone -f 440 -s 3`), set 25% and 100% from the menu, mute and unmute. F10 to F12 still work.
9. Open the menu with Command-Space and use the arrow keys, Return and Escape only.
10. End Session from the menu.
