# The SiliconForest pointer set (DESK7)

One family of pointers for everything on the screen: the desk's own pointer, its frame edges and grabs,
the dock, native Chonkstep clients and Mac (AppKit) applications. Black arrows, I-beams and resize arrows
with a white outline; white hands with a black outline. Original drawings (no Apple cursor image was used).

- `make_cursors.py` is the source: every shape is built from a few closed paths on a 24x24 grid of logical
  units (one unit = one pixel at 100 %), drawn as an outline stroke under a body fill.
  `python3 make_cursors.py svg` writes `svg/<name>.svg`; the compositor's copy lives in its build source
  (`crates/wm-darwin/cursors/*.svg` and `src/cursor_set.rs`, written by
  `python3 make_cursors.py <src>/crates/wm-darwin/cursors <src>/crates/wm-darwin/src/cursor_set.rs ../cursors`).
- 34 shapes, named as `wp_cursor_shape_v1` (CSS) names them: default, text, vertical-text, pointer, grab,
  grabbing, ew-/ns-/nwse-/nesw-resize, col-/row-resize, e/se/s/sw/w/nw/n/ne-resize, move, all-scroll,
  crosshair, cell, wait, progress, zoom-in, zoom-out, copy, alias, not-allowed, no-drop, help, context-menu.
- The compositor (`crates/wm-darwin/src/cursors.rs`) renders them with tiny-skia at the display's scale
  (24 px at 100 %, 36 at 150 %, 48 at 200 %), the hotspot on a pixel corner; it draws its own pointer from
  the set, answers `wp_cursor_shape_v1`, and recognises well-known client images (libwayland-cursor's
  built-in set, which AppKit's WindowServer loads without a theme, and this set's own Xcursor theme) and
  draws the set's shape instead, crisp.
- The Xcursor theme: `chonkstep-darwin-desktop --write-cursor-theme <dir>` (run on the M1, by the installed
  binary, so its images are bit for bit the ones the compositor recognises); installed as
  `/usr/share/icons/default` (`desk_m1.sh cursors`), it is what libwayland-cursor loads for every client
  that asks a theme for a cursor name.
