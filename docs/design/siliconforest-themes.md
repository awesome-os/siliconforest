# SiliconForest themes: Clearing, Canopy, Wafer, Die Shot

Four showcase themes for the SiliconForest desk (agent THEME, 17 November 2026). This file is the
creative brief, the reference for what each theme controls, the guide to adding a theme, and the
shot list for the screen recording. What is installed on the M1, with hashes and the way back, is
in `docs/roadmap/issues/SF-Z-desktop-night.md`, section "THEME".

## 1. The brand idea

SiliconForest is two materials that grow in layers: a forest (canopy, understory, moss, mist) and a
chip (metal layers, traces, vias, pads, the wafer under them). The mark already says it: a conifer
whose three tiers are circuit traces with vias at the tips, standing on a row of chip pads. The
themes take four views of that one object, from far to near:

| View | Theme | id | Mood |
|---|---|---|---|
| the forest at dawn, from a ridge | **Clearing** | `sf-clearing` | light, calm, the one to live in |
| the forest at night, from under the trees | **Canopy** | `sf-canopy` | dark, deep green, copper light |
| the wafer on the bench, in a clean room | **Wafer** | `sf-wafer` | light, cool, exact, a trace of iridescence |
| the die under the microscope | **Die Shot** | `sf-dieshot` | dark, gold on black, the demo piece that takes liberties |

Rules for the family:
- **Mac muscle memory.** Close, minimise, zoom sit at the top left in that order in all four; the
  title is where a Mac puts it; nothing a hand has learnt moves. The liberties are in the *shape* of
  the controls (a disc, a via, a pad, a bond pad), never in their place or order.
- **The apps are the content.** Terminal, Activity Monitor and Firefox draw their own insides (and
  their own menus). The chrome is quiet enough that a light app in a dark theme, or the reverse, looks
  intended: title bars are near-neutral with a tint of the theme, one accent per theme, no gradients
  that compete with a toolbar underneath.
- **One accent each**, used for focus, selection in the desk's own menus, the dock's LED ink and
  the running-application mark: moss green, copper, thin-film indigo, bond-wire gold.
- **It costs nothing.** Every effect is either drawn once and cached (title bands, dock tiles,
  wallpaper) or a few hundred thousand shaded pixels a frame (the shadow's ring). Nothing blurs the
  screen; nothing animates while the desk is still.

## 2. The four concepts

Sizes are in points at 100 %; the owner's desk runs at 150 % (state `scale` = 180), where 1 pt is
1.5 px. Contrast ratios are WCAG 2 relative-luminance ratios, computed from the values below
(`integrations/siliconforest/desktop/themes/contrast.py` prints the table).

### Clearing (`sf-clearing`, light)
*The forest at dawn: mist between ridgelines, the first light on moss.* The native theme done
properly: what the default would be if it had been designed for a Mac user.

| Token | Value | Against | Ratio |
|---|---|---|---|
| title bar (active) | `#e9ede5` | | |
| title bar (inactive) | `#f1f3ee` | | |
| panel / menus | `#fbfcf9` | | |
| text | `#1f2a24` | title bar | 12.5 |
| muted text (inactive title) | `#56625a` | inactive title | 5.7 |
| hairline | `#b9c3b8` | | |
| accent: moss | `#2c7a54` | panel | 5.1 (white on it: 5.2) |
| selection | `#cfe6d8` | text on it | 11.3 |
| close / minimise / zoom | `#cf4a3a` / `#a87004` / `#2f8a52` | title bar | 3.8 / 3.6 / 3.6 |
| controls, window inactive | `#7c877d` (ring) | inactive title | 3.4 |

- Type: Inter Medium 13 pt, centred over the window (as macOS), inactive in muted.
- Controls: 12 pt discs, 8 pt apart, 8 pt from the left edge, optically centred on the title's
  x-height; a 0.5 pt darker rim of the same hue. Hovering any of the three shows all three glyphs
  (x, minus, plus) in the hue at 55 % luminance; pressed darkens the disc 12 %. An inactive window's
  controls are hollow rings in neutral, and colour again when the pointer is over them.
- Frame: title bar 28 pt, radius 10 pt on all four corners, a 1 px hairline, a 1 px lighter top edge
  inside the title bar. Shadow: 0 / 10 pt offset, 40 pt blur, `#16241b` at 40 % for the focused
  window, 24 % unfocused (the first draft's 30 % did not lift a white window off the mist).
- Motion: a window fades in over 180 ms while rising 8 pt, ease-out cubic. Theme changes cross-fade
  over 220 ms.
- Dock and clock: paper tiles (`#f1f3ee`) with moss LED ink on dark-green glass; digital clock.
- Wallpaper `sf-clearing`: six ridgelines of conifers receding into mist, drawn from the mark's
  own tier shape, pale sage to warm white at the horizon, the sun as a soft via low on the right.
  The lower third (where the nearest, darkest ridge is) stays under the windows' feet; the upper two
  thirds are almost flat mist.

### Canopy (`sf-canopy`, dark)
*The forest at night, from under the trees: black conifers either side of a gap of sky, copper
light on the horizon.*

| Token | Value | Against | Ratio |
|---|---|---|---|
| title bar (active) | `#17221d` | | |
| title bar (inactive) | `#121a16` | | |
| panel / menus | `#111a16` | | |
| text | `#e6ede4` | title bar | 13.7 |
| muted text | `#9aa89d` | inactive title | 7.1 |
| hairline | `#2e3d35` | | |
| accent: copper | `#dd8f55` | panel | 6.9 (dark on it: 7.2) |
| selection | `#2b3d33` | text on it | 9.7 |
| close / minimise / zoom | `#ec7c60` / `#e2b65c` / `#82c98f` | title bar | 5.9 / 8.6 / 8.3 |
| controls, window inactive | `#5d6e64` | inactive title | 3.3 |

- Type: Inter SemiBold 13 pt, centred.
- Controls: **vias**. 12 pt rings, 1.75 pt wall, in the three hues, the hole showing the title bar
  (a plated through-hole). Hover fills the ring and shows the glyph in the title bar's colour;
  pressed is the filled via 15 % lighter. Inactive: neutral rings.
- Frame: title bar 30 pt, radius 12 pt, hairline `#2e3d35`, and a 1 px copper line along the top of
  the focused window only (the horizon's light, caught on an edge). Shadow: 0 / 14 pt, 48 pt blur,
  `#010403` at 70 % focused, 45 % unfocused: on a dark desk the shadow has to be nearly black to read.
- Motion: 200 ms fade, 10 pt rise, ease-out quint (slower, heavier). Theme cross-fade 260 ms.
- Dock and clock: dark bark tiles (`#18231e`), copper LED ink; digital clock.
- Wallpaper `sf-canopy`: tall conifers standing at both sides of the picture, the tallest at the
  edges (the busy part is at the rim, where the dock and the screen's edge are), a deep blue-green
  night sky between them with stars, six of them joined by faint traces into the mark's
  constellation, low ridges against a copper glow on the horizon. (A first version looked straight
  up at crowns leaning in from all four edges; the crowns read as fronds, not conifers, and it was
  dropped.)

### Wafer (`sf-wafer`, light)
*The clean room: a polished wafer under white light, the colours of a thin film where the light
catches it.*

| Token | Value | Against | Ratio |
|---|---|---|---|
| title bar (active) | `#f1f3f7` | | |
| title bar (inactive) | `#f6f7f9` | | |
| panel / menus | `#ffffff` | | |
| text | `#1c2330` | title bar | 14.2 |
| muted text | `#566074` | inactive title | 5.9 |
| hairline | `#c3cad6` | | |
| accent: thin-film indigo | `#4a5bd0` | panel | 5.7 (white on it: 5.7) |
| selection | `#dde2fa` | text on it | 12.3 |
| close / minimise / zoom | `#d2475a` / `#a87400` / `#1f8a5b` | title bar | 4.0 / 3.7 / 3.9 |
| controls at rest / inactive | `#737c8c` | inactive title | 3.9 |

- Type: IBM Plex Sans 13 pt (bundled, OFL), centred; the engineering-drawing voice.
- Controls: **pads**. 11 pt squares with a 3 pt radius, 8 pt apart. At rest they are graphite
  (`#737c8c`): a clean room has no coloured lights. When the pointer enters the group all three take
  their hues and glyphs. Pressed darkens 12 %. Inactive: hollow.
- Frame: title bar 28 pt, radius 7 pt (crisper than Clearing), hairline `#c3cad6`. Every window
  carries the **interference line** along its top edge: teal `#19a3a8` to indigo `#4a5bd0` to violet
  `#9a5bd6`, left to right; 3 pt at full strength on the focused window, 2 pt paled 62 % toward the
  hairline on the others. It is the only gradient in the family and the theme's signature. (The
  first build drew it 2 pt on the focused window only: in a picture of the whole desk the theme
  read as "light with a blue accent"; the coordinator's remark, and right.) Shadow: 0 / 7 pt, 28 pt
  blur, `#141d38` at 34 % focused, 20 % unfocused: tighter and cooler, light from straight above.
- Motion: 140 ms fade, 4 pt rise, ease-out cubic: quick and exact. Theme cross-fade 200 ms.
- Dock and clock: white enamel tiles, indigo LED ink on blue-black glass; the analog clock suits it
  (the owner's stored clock style is not changed).
- Wallpaper `sf-wafer`: a wafer far larger than the screen entering from the lower right; its
  edge crosses the top of the screen right of the middle and the bottom near the left, so the
  right-hand column, the bottom strip and the gaps beside the windows are wafer: whole dies only, as
  on a wafer map, each with the same faint layout, the film's colour turning with the angle
  (strongest in a band inside the edge, one fine line of it at the bevel), one broad reflection, the
  mark etched on a drop-in site beside the instrument column. The launchers' corner is plain bench.
  (The first version showed a small arc in the lower right, hidden by any window there.)

### Die Shot (`sf-dieshot`, dark)
*The die under the microscope: metal layers, bond pads, gold wire on black.* The liberty theme.

| Token | Value | Against | Ratio |
|---|---|---|---|
| title bar (active) | `#12161c` | | |
| title bar (inactive) | `#0e1116` | | |
| panel / menus | `#0e1116` | | |
| text | `#e9eef2` | title bar | 15.5 |
| muted text | `#93a0ad` | inactive title | 7.1 |
| hairline | `#2c3540` | | |
| accent: bond-wire gold | `#e7c15a` | panel | 11.0 (dark on it: 10.9) |
| selection | `#26303a` | text on it | 11.5 |
| close / minimise / zoom | `#ff7a6b` / `#e7c15a` / `#58d6a0` | title bar | 7.1 / 10.5 / 10.0 |
| controls, window inactive | `#5b6876` | inactive title | 3.3 |

- Type: Source Code Pro Medium 12 pt, **left**, directly after the controls, like a net label on a
  schematic. This is the one theme where the title is not centred.
- Controls: **bond pads**. 12 pt squares, 1.5 pt radius, outlined 1.5 pt in the three hues with a
  solid 4 pt core (pad and probe mark). Hover fills the pad; the glyph is cut in the title bar's
  colour. Pressed: filled, 15 % lighter. Same place, same order as a Mac.
- Frame: title bar 30 pt, radius 3 pt (a die is sawn, not moulded), a 1 px gold border on the
  focused window, `#2c3540` otherwise, and a **trace**: a 1 px gold line from the end of the title to
  8 pt before the right edge, ending in a via (5 pt ring). It is the one decoration in the family,
  and it is why the title is at the left. Shadow: 0 / 10 pt, 32 pt blur, black at 80 % focused, 50 %
  unfocused.
- Motion: 120 ms fade, no rise, ease-out expo: a window is *there*, like a probe touching down.
  Theme cross-fade 180 ms.
- Dock and clock: black tiles with a gold hairline, gold LED ink; digital clock.
- Wallpaper `sf-dieshot`: an invented floorplan (no real chip): blocks cut recursively, each made
  of a few macros with their own texture (memory arrays, standard-cell rows, bit slices, analog
  plates) lit from the upper left, power straps of two top metals over everything, gold routing in
  the channels, a seal ring, and a ring of small bond pads outside it; one thin-film reflection
  lies across the die, its colour turning along it; the mark is laid out in the top metal at the
  centre like a mask-shop signature, a dark body with a bright core line. The detail is at the
  macros' rims; their middles are a quiet tone. The whole is darkened and desaturated a little: a
  wallpaper, not a photograph. (The first version had flat single-texture blocks and a heavier pad
  ring: muddy, the coordinator said, and it was.)

### How they differ from each other and from what was there

| | Clearing | Canopy | Wafer | Die Shot | washi / obsidian / relay (existing) |
|---|---|---|---|---|---|
| appearance | light | dark | light | dark | light / dark / dark |
| controls | discs | vias (rings) | pads (rounded squares), grey at rest | bond pads (outlined squares) | line glyphs at the right |
| controls' place | left | left | left | left | right |
| title | centred, Inter Medium | centred, Inter SemiBold | centred, Plex Sans | left, Source Code Pro | left |
| radius | 10 | 12 | 7 | 3 | 0 / 3 / 9 (top corners only) |
| focus signal | shadow depth, coloured controls | copper top line | interference line at full strength | gold border + trace | accent square / title colour |
| shadow | soft, green-black 40 % | deep, black 70 % | tight, cool 34 % | dense black 80 % | none drawn on this desk |
| motion | 180 ms, rise 8 | 200 ms, rise 10 | 140 ms, rise 4 | 120 ms, no rise | none |
| accent | moss `#2c7a54` | copper `#dd8f55` | indigo `#4a5bd0` | gold `#e7c15a` | red / amber / periwinkle |

## 3. Wallpapers

All four are original and drawn by code in this repository
(`integrations/siliconforest/desktop/themes/wallpapers/make_wallpapers.py`, numpy + Pillow): no
photograph, no third-party image, no font. Each is rendered at 2560x1600 (the panel) and can be
rendered at 5120x3200 (`--scale 2`). Gradients are computed in floating point and dithered with
triangular noise of one 8-bit step before quantising, so no band is visible on the panel; the files
are JPEG at quality 98, 4:4:4 (measured: at 93 the codec flattens the dither into runs of up to 111
equal pixels on Wafer's bench, at 98 the longest run is 23; a PNG of the same picture is 2.8 MB
against 0.7 MB). The busy part of every picture is kept away from the middle of the
screen, and each was checked with windows on top.

## 4. What a theme controls on this desk

A theme is a `wm_theme::Theme` value built in code (`crates/wm-theme/src/`; the compositor
`chonkstep-darwin-desktop` and the dock `chonkstep-dock` both compile it in, so a new theme needs
both rebuilt). The SiliconForest themes are in `forest.rs` and are drawn by the modern recipe
(`styles/modern.rs`) with the window tokens of `modern::WindowChrome`:

| What | Where it comes from | Who draws it |
|---|---|---|
| title bar height, corner radius, border, padding, control box and gap | `Chrome.frame` (`FrameMetrics`) | recipe, cached per title/focus/state |
| title bar and menu colours, text, hairline, accent, selection | `Chrome` tokens (`surface`, `raised`, `panel`, `text`, `muted`, `line`, `accent`, ...) | recipe |
| controls: shape, side, order, hues, idle colour, grey-at-rest | `WindowChrome.controls`, `.left`, `.neutral_at_rest`, `.close`/`.minimise`/`.zoom`/`.idle`; order: `Theme.titlebar.buttons` | `styles/forest.rs` |
| title: face, weight (Normal, Medium, SemiBold, Bold), size, centred or left | `Theme.titlebar.font`, `WindowChrome.centered` | `styles/forest.rs`, `paint::draw_title_text` (capitals' height centred: every title on the same line) |
| top edge: highlight, accent rim, interference line | `WindowChrome.top_line` | `styles/forest.rs` |
| the trace and its via | `WindowChrome.trace` | `styles/forest.rs` |
| a hairline of one device pixel at every scale | `WindowChrome.hairline` | `Chrome::scaled` |
| shadow: offset, blur, colour, alpha with and without focus | `Chrome.shadow`, `WindowChrome.shadow_unfocused` | compositor (`wm-darwin/src/effects.rs`), only for `WindowChrome.effects` |
| rounded lower corners with the border's ring | `Chrome.frame.radius`, `WindowChrome.effects` | compositor (`effects.rs`) |
| a window's appearing: duration, rise, curve | `WindowChrome.open_ms`, `.open_rise`, `.open_curve` | compositor (`motion.rs`), policy `motion` |
| theme change: cross-fade duration | `WindowChrome.switch_ms` | compositor (`motion.rs`, `glrender.rs` `fade_begin`) |
| the desk's menus, notices, switcher, launcher, minimised tiles | `Theme.menu`, `Theme.tile`, the `Chrome` tokens | shell (as for every theme) |
| dock tiles, LED ink and glass, launcher mark, clock faces | `Theme.tile`, `Theme.terminal.cursor` (the accent), `Chrome.frame.radius` | dock (as for every theme) |
| wallpaper | `Theme.wallpaper` = the theme's id; the picture is `<id>.jpg` in the host's backgrounds (`chonk-shell` `Wallpaper::Forest`), else the theme's `background` colour | shell |
| appearance | each is drawn for one (`default_theme::standalone_appearance`); the other appearance takes the sibling's palette | |

Not themed, and why:
- **The pointer.** One SiliconForest set for every theme (DESK7): a pointer that changes with the
  theme is a pointer the hand has to find again.
- **Applications' own drawing.** Terminal, Activity Monitor and Firefox draw their content, toolbars
  and menus themselves (AppKit); the accent does not reach their selection colour. That needs AppKit
  to read the desk's accent: asked of nobody yet, noted in SF-Z "THEME".
- **A window's leaving.** Closing and minimising are not animated: the picture of a window that has
  gone belongs to its application and is no longer there to fade.
- **The existing themes.** washi, obsidian, relay and the others are drawn exactly as before:
  none has `effects`, a control shape or motion (their tokens are the defaults), so the owner's desk
  does not change until he picks a SiliconForest theme.

## 5. Adding a theme

1. In `crates/wm-theme/src/forest.rs` add a palette (twelve tokens, four control colours, the
   shadow's colour and two alphas) and a row in `CHOICES`, and give it its metrics, type, controls
   and motion in the `match id` tables of `theme()`. A light theme and a dark one that share shapes
   can be each other's companion, as Clearing/Canopy are.
2. Register it in `default_theme.rs`: `CHOICES` (both lists, and their lengths), `all_themes()`,
   and the light list of `native_renditions_are_what_each_theme_originally_shipped_as` if it is light.
3. `cargo test -p wm-theme`: `forest::tests::text_and_controls_keep_their_contrast` holds the
   palette to 4.5:1 for text and 3:1 for controls; the registry tests hold the lists together.
4. Look at it: `cargo run -p wm-theme --example forest_preview -- <dir> 1.5` (the chrome in every
   state), `DOCK_PREVIEW_DIR=<dir> DOCK_PREVIEW_THEMES=<id> cargo test -p chonk-dock column_preview
   -- --ignored` (the dock's tiles), and `tests/theme_gallery.rs` (the whole desk off screen with
   pictures for window contents).
5. A wallpaper: a function in `integrations/siliconforest/desktop/themes/wallpapers/make_wallpapers.py`
   and a name in its table; the file `<id>.jpg` goes to `/usr/share/backgrounds/siliconforest`
   (`desk_m1.sh wallpapers`). Without it the theme shows its `background` colour.
6. Build the compositor (both builds) and the dock, install, relaunch
   (`integrations/siliconforest/terminal-app/compositor/README.md`).

A new *kind* of control or top edge is a variant of `modern::Controls` / `modern::TopLine` and an
arm in `styles/forest.rs`; nothing else knows theme ids.

## 6. What was added to the desk, and why

All of it is compositor patch 36 (`integrations/siliconforest/terminal-app/compositor/`); the existing
themes do not use any of it.

| Addition | Why | Cost |
|---|---|---|
| Window tokens (`modern::WindowChrome`) and the band painter `styles/forest.rs` | the recipe had one kind of control (line glyphs at the right) and a title after a focus mark; a Mac user's hand goes to the top left | a title band is drawn once per title, focus and control state and cached (16 bands), as before |
| `paint::draw_title_text` | titles were centred by their own ink: "zsh" sat lower than "Terminal". Now the capitals' height is centred: one line for every title | none |
| Font weights Medium and SemiBold; Inter (500, 600) and Source Code Pro (500) read from SiliconForest's font files when the session's fonts are set up | the session gives the desk two DejaVu faces only; a theme change must read no file (the desk's rule, held by a test) | 1.4 MB of font bytes held by the compositor |
| Shadows (`wm-darwin/src/effects.rs`) | this desk drew none: windows were flat rectangles on the wallpaper | eight small pictures per shadow, made once (Canopy at 150 %: 1.3 MB for both focus states; the others less); per frame only the pieces inside what is repainted are drawn, as ordinary blended pictures. No pass over the screen, no blur at run time |
| Rounded lower corners (`effects.rs`) | the upper corners were rounded by the title band, the lower ones were the application's square pixels | a window whose pixels the desk holds: two pictures of radius x radius pixels remade when its content changes; a client GPU buffer: a few instructions in the fragment shader inside the two corner squares |
| A window's appearing (`motion.rs`) | a window was simply there | while a window appears (120 to 200 ms) its pieces are redrawn each frame with an alpha; nothing otherwise |
| Theme changes cross-fade (`motion.rs`, `glrender.rs` `fade_begin`) | a change of theme was a cut, with a pause while the desk dressed | one copy of the screen (a GPU texture, 16 MB) for the length of the fade, and whole-screen frames while it lasts (13 to 16 frames) |
| `--desktop-request theme <id>` / `wallpaper <id>`, `desk_m1.sh theme` / `wallpaper` | the only way to change theme was the menu at the M1; a recording wants the change without a menu on the screen | none |
| `Wallpaper::Forest`: a theme's own picture by the theme's id, from a file | the built-in pictures are compiled into the binary; these are 0.7 to 1.3 MB each and can be redrawn without a build | the picture is decoded when the theme is put on (part of the ~250 ms the desk takes to dress) |
| `default_theme::standalone_appearance` | SiliconForest's desk is always "dark": Clearing and Wafer would have come up in their dark siblings' palettes | none. The appearance change signals no terminal and starts no program on this desk (it would have: found before it ran) |
| Policy `motion=on|off` | a switch for the two animations without a relaunch | none |

All three renderers (the GL one the M1 uses, and the two CPU ones the tests compare it with) draw the
same pixels for shadows and corners: the shadow's pieces and the corner pictures are 8-bit pictures
blended with the GPU's own arithmetic (`effects::blend_piece`), and
`tests/wayland_theme.rs` holds every theme at four scales to exact equality.

## 7. What it costs (measured on the M1)

The owner's desk at 150 %, `partial_repaint=on`, my own Firefox showing a page with a small animation
(a frame every refresh) beside my own Terminal and Activity Monitor; each look worn for 22 s through
the test instrument; the desk's own `WMCOST` line for a 10 s window inside each (render + GPU finish
per frame). Build theme-003, 17 November 09:02Z, n=1 per row (the chrome's drawing is the same in
theme-005; only title faces and Wafer's top line changed since)
(`runs/m1-desktop-010/theme-tests/m1/p1.txt`):

| Look | frames/s | render+finish p50 | p95 | p99 | compositor CPU s per 22 s |
|---|---|---|---|---|---|
| washi (before) | 57.4 | 1.1 ms | 2.1 | 3.5 | 2.85 |
| Clearing | 57.3 | 1.2 | 2.0 | 3.9 | 3.04 |
| Canopy | 57.6 | 1.2 | 2.1 | 4.3 | 3.58 |
| Wafer | 57.3 | 1.2 | 1.9 | 4.2 | 2.96 |
| Die Shot | 57.4 | 1.2 | 1.9 | 3.6 | 2.73 |
| washi (after) | 57.4 | 1.2 | 1.8 | 3.4 | 3.28 |

A second run on theme-004 with washi between the looks (`p3.txt`, 21 s each): washi 57.7, 57.8, 57.8
frames/s with p50 1.1 ms and p95 1.7-1.8; Wafer 57.5, 1.2 / 1.9; Die Shot 57.6, 1.2 / 2.1. No difference
beyond the spread between two runs of the same look.

Memory (RSS, same run): the compositor 142.6 MB in washi before any theme change, then Wafer 155.4,
washi 153.6, Die Shot 157.0, washi 153.9: a SiliconForest theme is 2 to 3 MB over washi at the same
point; the first theme change of a session leaves the process about 11 MB larger. The dock is
61.9 MB with every look.

Motion, seen frame by frame in recordings the desk made of itself: a theme change pauses the
picture about 250 ms while the desk dresses (the wallpaper's decode, every title band redrawn; 90 to
180 ms for the existing themes, DESK7), then cross-fades over 13 frames at 60 a second, a smooth
ease-out; a window that appears in Clearing comes up over 10 frames (170 ms). One defect left in:
the rise is 12 pt on the M1 where the brief says 8 (and 15 for Canopy's 10, 6 for Wafer's 4): the
distance is scaled for the display twice. It reads well; it is not what was written.

A run made while the display was asleep (`p2.txt`) is not in the table: nothing is drawn then.
It showed something else, recorded in SF-Z "THEME" for the desk's owner: with the display asleep and
a client that keeps drawing, the compositor uses 57 % of a core, whatever the theme.

## 8. The demo: a 75-second shot list

**Before recording.** Terminal, Activity Monitor and Firefox open on one Space with room around
them (the wallpapers are quiet in the middle and say most at the bottom and the sides): Firefox at
the upper left with a real page, Activity Monitor lower right, Terminal lower left in front.
A second Space left empty. Theme Clearing. The pointer away from the dock (a resting pointer
brings up a launcher's name).

**Recording.** Command-Shift-5, Record Screen, Record (full size, 60 frames a second; Options has
both). Stop: Command-Control-Escape. The movie is in `~/Pictures/Screenshots`.

**Changing theme without a menu in the picture**, from the workstation (the change cross-fades):

```sh
T=integrations/siliconforest/desktop/tools/desk_m1.sh
$T theme sf-clearing     # also: sf-canopy, sf-wafer, sf-dieshot
```

or at the M1 with the menu in the picture: Command-Space, Theme, one of the last four rows. The
whole tour on a timer, for a hands-free take (start it, then start recording):

```sh
for t in sf-clearing sf-canopy sf-wafer sf-dieshot sf-clearing; do $T theme $t; sleep 15; done
```

Afterwards, the look of before: `$T theme washi; $T wallpaper host:siliconforest`.

| Time | Theme | What is on screen | What it shows |
|---|---|---|---|
| 0:00 - 0:05 | Clearing | the desk at rest; the clock's colon ticks | the family's calm face: mist, moss, a Mac's window |
| 0:05 - 0:15 | Clearing | click Terminal's tile in the dock: a new window fades in, rising. Type `neofetch`. Click Activity Monitor, then Firefox, then Terminal | the appearing (180 ms); focus moving: controls take and lose their colours, the shadow deepens under the front window |
| 0:15 - 0:20 | Clearing | pointer onto the three discs: the glyphs appear; minimise Terminal to its tile, click the tile to bring it back | Mac muscle memory: the controls are where the hand goes |
| 0:20 - 0:22 | to Canopy | `theme sf-canopy` | the cross-fade: day to night |
| 0:22 - 0:35 | Canopy | scroll the page in Firefox with two fingers; drag Activity Monitor by its title bar across the trees; pointer onto the vias (they fill) | a busy application under a dark theme at full frame rate; the copper rim on the focused window; the shadow on a dark desk |
| 0:35 - 0:37 | to Wafer | `theme sf-wafer` | night to the clean room |
| 0:37 - 0:50 | Wafer | click through the three windows; pointer onto the pads (graphite, then colour); Activity Monitor's CPU graph running | the interference line jumping to the focused window; exactness |
| 0:50 - 0:52 | to Die Shot | `theme sf-dieshot` | the bold one arrives |
| 0:52 - 1:07 | Die Shot | open a second Terminal window (it is there at once: 120 ms); three-finger swipe to the empty Space: the die alone, the mark in its top metal; swipe back | the liberties: bond pads, the title as a net label with its trace and via; the wallpaper as a piece of its own |
| 1:07 - 1:15 | to Clearing | `theme sf-clearing`; pointer to rest away from the windows | the close: back to the morning, the dock's clock in the last frame |

Notes for the take: each theme change pauses the picture for about a quarter of a second before it
fades (the desk is dressing); change theme while nothing else is moving and the pause is invisible.
While recording the desk draws the pointer into the frames (it leaves its plane): expect it a frame
later than usual. If Firefox stutters under the recorder, DESK11's
section of SF-Z has the recorder's settings (`record_nice`, `record_crf`).
