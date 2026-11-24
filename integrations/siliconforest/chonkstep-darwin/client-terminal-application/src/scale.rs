//! Output scale: which scale applies, the cell metrics in device pixels at
//! that scale, and the buffer / logical sizes committed for a grid. A scale is
//! a whole number of 120ths (`wp_fractional_scale_v1`); all arithmetic here is
//! in integers, so the compositor can reproduce every size exactly.
use crate::term::{MAX_COLS, MAX_ROWS, MIN_COLS, MIN_ROWS};
/// Scale 1 in 120ths.
pub const UNIT: u32 = 120;
/// Font size in logical pixels unless `FONT_ENV` says otherwise.
pub const BASE_FONT_PX: u32 = 18;
/// Environment variable: the font size in logical pixels, a whole number 6..=96.
pub const FONT_ENV: &str = "CHONK_TERMINAL_FONT_PX";
/// IBM Plex Mono: advance of every glyph and units per em (checked against the face).
pub const ADVANCE_UNITS: u32 = 600;
pub const UNITS_PER_EM: u32 = 1000;
/// Padding around the grid, logical pixels on each side.
const PAD_LOGICAL: u32 = 8;
/// Rounded division of non-negative integers, halves away from zero.
fn rdiv(n: u64, d: u64) -> usize {
    ((2 * n + d) / (2 * d)) as usize
}
/// The font size asked for through the environment, or the default.
pub fn font_px(value: Option<&str>) -> u32 {
    value.and_then(|v| v.parse::<u32>().ok()).filter(|px| (6..=96).contains(px)).unwrap_or(BASE_FONT_PX)
}
/// The effective scale in 120ths. Precedence: the fractional protocol's
/// preferred scale (pass `None` unless that protocol and a viewport are in
/// use), then `wl_surface.preferred_buffer_scale`, then the scale of the
/// output the surface entered, then 1. Values outside 1/2..=8 are not used.
pub fn effective(fractional: Option<u32>, preferred: Option<i32>, output: Option<i32>) -> u32 {
    let whole = |n: i32| (1..=8).contains(&n).then(|| n as u32 * UNIT);
    fractional
        .filter(|s| (UNIT / 2..=8 * UNIT).contains(s))
        .or_else(|| preferred.and_then(whole))
        .or_else(|| output.and_then(whole))
        .unwrap_or(UNIT)
}
/// The protocol's rule for the buffer size of a logical size: round half away from zero.
pub fn to_device(logical: usize, scale120: u32) -> usize {
    rdiv(logical as u64 * u64::from(scale120), u64::from(UNIT))
}
/// The smallest logical size whose scaled size is at least `device` pixels.
pub fn to_logical(device: usize, scale120: u32) -> usize {
    (device * UNIT as usize).div_ceil(scale120 as usize)
}
/// For `grid` device pixels on one axis: the logical size L = ceil(grid / S)
/// and the buffer size B = round(L x S). B >= grid; the pixels left over
/// (fewer than S + 1/2) are background.
pub fn snap(grid: usize, scale120: u32) -> (usize, usize) {
    let logical = to_logical(grid, scale120);
    (logical, to_device(logical, scale120))
}
/// Cell metrics in whole device pixels at one scale.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Metrics {
    pub scale120: u32,
    /// The font's pixel size (em) in device pixels, times 120: base x scale120.
    pub font_px120: u32,
    pub cw: usize,
    pub ch: usize,
    /// Baseline, device pixels below the top of the cell.
    pub baseline: usize,
    /// Padding on each side of the grid.
    pub pad: usize,
    /// Thickness of the underline, the missing-glyph box and the bold smear.
    pub stroke: usize,
}
impl Metrics {
    /// `base` is the font size in logical pixels. The cell is the font's
    /// advance wide, 22/18 em high with the baseline at 17/18 em (11 x 22
    /// and 17 at 18 px), each computed at the device size and then rounded.
    pub fn new(base: u32, scale120: u32) -> Self {
        let font = u64::from(base) * u64::from(scale120);
        let em = |n: u64| rdiv(n * font, 18 * u64::from(UNIT));
        Self {
            scale120,
            font_px120: font as u32,
            cw: rdiv(u64::from(ADVANCE_UNITS) * font, u64::from(UNITS_PER_EM) * u64::from(UNIT)).max(1),
            ch: em(22).max(1),
            baseline: em(17),
            pad: to_device(PAD_LOGICAL as usize, scale120),
            stroke: em(1).max(1),
        }
    }
    /// The font size in device pixels.
    pub fn font_px(&self) -> f64 {
        f64::from(self.font_px120) / f64::from(UNIT)
    }
    /// `n` eighteenths of the em in device pixels (the drawing's proportions).
    pub fn em18(&self, n: usize) -> usize {
        rdiv(n as u64 * u64::from(self.font_px120), 18 * u64::from(UNIT))
    }
    /// Device pixels of a grid with its padding.
    pub fn grid_px(&self, cols: usize, rows: usize) -> (usize, usize) {
        (cols * self.cw + 2 * self.pad, rows * self.ch + 2 * self.pad)
    }
    /// Whole cells that fit in `device` pixels (not bounded).
    pub fn cells(&self, device: (usize, usize)) -> (usize, usize) {
        (device.0.saturating_sub(2 * self.pad) / self.cw, device.1.saturating_sub(2 * self.pad) / self.ch)
    }
    /// The cell under a pointer position given in logical surface units.
    pub fn cell_at(&self, x: f64, y: f64) -> (usize, usize) {
        let at = |v: f64, size: usize| {
            let device = (v.max(0.0) * f64::from(self.scale120) / f64::from(UNIT)) as usize;
            device.saturating_sub(self.pad) / size
        };
        (at(x, self.cw), at(y, self.ch))
    }
    /// The logical sizes a configure may ask for: those of the smallest and
    /// the largest grid. `tolerant` (a compositor that scales): an eighth
    /// more either way, because cell rounding differs between scales and a
    /// window sized at one scale must survive a change to another; `layout`
    /// bounds the grid.
    pub fn configure_bounds(&self, tolerant: bool) -> ((usize, usize), (usize, usize)) {
        let low = self.grid_px(MIN_COLS, MIN_ROWS);
        let high = self.grid_px(MAX_COLS, MAX_ROWS);
        let l = |v: usize| to_logical(v, self.scale120);
        if !tolerant {
            ((l(low.0), l(low.1)), (l(high.0), l(high.1)))
        } else {
            ((l(low.0) * 7 / 8, l(low.1) * 7 / 8), (l(high.0) * 9 / 8, l(high.1) * 9 / 8))
        }
    }
}
/// What is committed for one window state.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Layout {
    pub cols: usize,
    pub rows: usize,
    /// The buffer in device pixels.
    pub buffer: (usize, usize),
    /// The surface in logical units: window geometry and viewport destination.
    pub logical: (usize, usize),
    /// `wl_surface.set_buffer_scale`: 1 with a viewport, else the whole scale.
    pub buffer_scale: i32,
}
/// The grid and sizes for a configured logical size (`None`: the compositor
/// left the size to us, keep `keep` columns and rows). With `viewport` the
/// buffer is `to_device(logical)` per axis and the scale may be fractional;
/// without, the buffer is the logical size times the whole scale.
pub fn layout(m: &Metrics, configured: Option<(usize, usize)>, keep: (usize, usize), viewport: bool) -> Layout {
    // Without a viewport only whole scales can be presented.
    let s = if viewport { m.scale120 } else { m.scale120.div_ceil(UNIT) * UNIT };
    let available = configured.map(|(w, h)| (to_device(w, s), to_device(h, s)));
    let (cols, rows) = available.map_or(keep, |a| m.cells(a));
    let (cols, rows) = (cols.clamp(MIN_COLS, MAX_COLS), rows.clamp(MIN_ROWS, MAX_ROWS));
    let grid = m.grid_px(cols, rows);
    let axis = |grid: usize, configured: Option<usize>| {
        let (logical, buffer) = snap(grid, s);
        match configured {
            // ceil(grid / S) can be one more than a configured size whose
            // scaled size was rounded up to hold the grid: stay inside it.
            Some(c) if logical > c && to_device(c, s) >= grid => (c, to_device(c, s)),
            _ => (logical, buffer),
        }
    };
    let (w, h) = (axis(grid.0, configured.map(|c| c.0)), axis(grid.1, configured.map(|c| c.1)));
    Layout {
        cols,
        rows,
        buffer: (w.1, h.1),
        logical: (w.0, h.0),
        buffer_scale: if viewport { 1 } else { (s / UNIT) as i32 },
    }
}
/// The status line for unattended checks, printed whenever scale or grid change.
pub fn status_line(m: &Metrics, l: &Layout) -> String {
    format!(
        "TERMSCALE scale120={} font_px={} cell={}x{} grid={}x{} buffer={}x{} logical={}x{} pad={} buffer_scale={}",
        m.scale120,
        m.font_px(),
        m.cw,
        m.ch,
        l.cols,
        l.rows,
        l.buffer.0,
        l.buffer.1,
        l.logical.0,
        l.logical.1,
        m.pad,
        l.buffer_scale,
    )
}
/// Wheel travel (Wayland axis units, which are logical: 10 per detent) to
/// scrollback lines, three per detent; the remainder stays in `wheel`.
/// Positive lines scroll back. The output scale plays no part.
pub fn wheel_lines(wheel: &mut f64) -> isize {
    let detents = (*wheel / 10.0).trunc();
    *wheel -= detents * 10.0;
    -(detents as isize) * 3
}
#[cfg(test)]
mod tests {
    use super::*;
    /// The compositor's steps, and a few it does not use.
    const STEPS: [u32; 7] = [120, 150, 180, 210, 240, 300, 360];
    #[test]
    fn scale_selection_precedence() {
        assert_eq!(effective(None, None, None), 120);
        assert_eq!(effective(None, None, Some(2)), 240);
        assert_eq!(effective(None, Some(3), Some(2)), 360, "preferred_buffer_scale before the output");
        assert_eq!(effective(Some(150), Some(2), Some(2)), 150, "fractional before both");
        assert_eq!(effective(Some(240), None, None), 240);
        // Nonsense is skipped, not used: the next source applies.
        assert_eq!(effective(Some(0), Some(2), None), 240);
        assert_eq!(effective(Some(5000), None, Some(0)), 120);
        assert_eq!(effective(None, Some(-1), Some(2)), 240);
        assert_eq!(effective(None, Some(99), None), 120);
    }
    #[test]
    fn font_size_from_the_environment_is_bounded() {
        assert_eq!(font_px(None), 18);
        assert_eq!(font_px(Some("14")), 14);
        for bad in ["", "5", "97", "18.5", "-3", "big"] {
            assert_eq!(font_px(Some(bad)), 18, "{bad:?}");
        }
    }
    #[test]
    fn metrics_at_scale_one_are_the_original_cell_and_scale_with_the_font() {
        let m = Metrics::new(18, 120);
        assert_eq!((m.cw, m.ch, m.baseline, m.pad, m.stroke, m.font_px()), (11, 22, 17, 8, 1, 18.0));
        let expect = [(120, 18.0, 11, 22, 8), (150, 22.5, 14, 28, 10), (180, 27.0, 16, 33, 12), (210, 31.5, 19, 39, 14), (240, 36.0, 22, 44, 16), (300, 45.0, 27, 55, 20), (360, 54.0, 32, 66, 24)];
        for (s, font, cw, ch, pad) in expect {
            let m = Metrics::new(18, s);
            assert_eq!((m.font_px(), m.cw, m.ch, m.pad), (font, cw, ch, pad), "scale {s}");
            // Computed at the device size, then rounded: within half a pixel of exact.
            assert!((m.cw as f64 - 0.6 * font).abs() <= 0.5 && (m.ch as f64 - font * 22.0 / 18.0).abs() <= 0.5);
        }
        assert_eq!((Metrics::new(14, 120).cw, Metrics::new(14, 120).ch), (8, 17));
    }
    #[test]
    fn buffer_is_the_rounded_logical_size_and_holds_the_grid_at_every_step() {
        for s in STEPS {
            let m = Metrics::new(18, s);
            for cols in MIN_COLS..=200 {
                for rows in [MIN_ROWS, 7, 24, 25, 37, 60, MAX_ROWS] {
                    let grid = m.grid_px(cols, rows);
                    for (g, (logical, buffer)) in [(grid.0, snap(grid.0, s)), (grid.1, snap(grid.1, s))] {
                        // The protocol rule, in exact integers: B = floor(L x S + 1/2).
                        assert_eq!(buffer, (logical * s as usize + 60) / 120, "scale {s} grid {g}");
                        assert!(buffer >= g, "scale {s} grid {g}: buffer {buffer}");
                        assert!(buffer - g <= (s as usize).div_ceil(120), "scale {s} grid {g}: {} spare", buffer - g);
                        assert_eq!(logical, (g * 120).div_ceil(s as usize));
                    }
                    // With no configured size the layout is exactly that snap.
                    for viewport in [true, false] {
                        let l = layout(&m, None, (cols, rows), viewport);
                        assert_eq!((l.cols, l.rows), (cols, rows));
                        if viewport {
                            assert_eq!((l.logical, l.buffer, l.buffer_scale), ((snap(grid.0, s).0, snap(grid.1, s).0), (snap(grid.0, s).1, snap(grid.1, s).1), 1));
                        } else {
                            // No viewport: the whole scale above, buffer = N x logical.
                            let n = (s as usize).div_ceil(120);
                            assert_eq!(l.buffer_scale as usize, n);
                            assert_eq!(l.buffer, (l.logical.0 * n, l.logical.1 * n));
                            assert!(l.buffer.0 >= grid.0 && l.buffer.0 - grid.0 < n && l.buffer.1 >= grid.1 && l.buffer.1 - grid.1 < n);
                        }
                    }
                }
            }
        }
    }
    #[test]
    fn grid_for_a_configured_size_at_each_scale() {
        // 896 x 544 logical is the 80 x 24 window at scale 1.
        let expect = [(120, 80, 24), (150, 78, 23), (180, 82, 24), (210, 81, 23), (240, 80, 24), (300, 81, 24), (360, 82, 24)];
        for (s, cols, rows) in expect {
            let m = Metrics::new(18, s);
            let l = layout(&m, Some((896, 544)), (1, 1), true);
            assert_eq!((l.cols, l.rows), (cols, rows), "scale {s}");
            // By hand: floor((round(W x S) - 2 pad) / cell).
            let available = ((896 * s as usize + 60) / 120, (544 * s as usize + 60) / 120);
            assert_eq!((cols, rows), ((available.0 - 2 * m.pad) / m.cw, (available.1 - 2 * m.pad) / m.ch));
        }
        // Every configured size: the commit stays inside it, obeys the rule and holds the grid.
        for s in STEPS {
            let m = Metrics::new(18, s);
            for w in (300..1400).step_by(7).chain(1279..=1281) {
                for h in [200, 333, 544, 799, 800, 801] {
                    let l = layout(&m, Some((w, h)), (1, 1), true);
                    let grid = m.grid_px(l.cols, l.rows);
                    assert!(l.logical.0 <= w && l.logical.1 <= h, "scale {s} {w}x{h}: {l:?}");
                    assert_eq!(l.buffer, (to_device(l.logical.0, s), to_device(l.logical.1, s)));
                    assert!(l.buffer.0 >= grid.0 && l.buffer.1 >= grid.1);
                    assert!(l.buffer.0 - grid.0 <= (s as usize).div_ceil(120) && l.buffer.1 - grid.1 <= (s as usize).div_ceil(120));
                    // One more column or row would not fit the configured size.
                    assert!(grid.0 + m.cw > to_device(w, s) && grid.1 + m.ch > to_device(h, s));
                    // Committing the same logical size again changes nothing.
                    assert_eq!(layout(&m, Some(l.logical), (1, 1), true), l);
                }
            }
        }
        // A size too small or too large for the bounded grid is bounded, not followed.
        let m = Metrics::new(18, 240);
        assert_eq!((layout(&m, Some((10, 10)), (1, 1), true).cols, layout(&m, Some((10, 10)), (1, 1), true).rows), (MIN_COLS, MIN_ROWS));
        assert_eq!(layout(&m, Some((90_000, 90_000)), (1, 1), true).cols, MAX_COLS);
        // Whole scale without a viewport: buffer = N x logical inside the configured size.
        let l = layout(&m, Some((896, 544)), (1, 1), false);
        assert_eq!((l.cols, l.rows, l.logical, l.buffer, l.buffer_scale), (80, 24, (896, 544), (1792, 1088), 2));
        let l = layout(&Metrics::new(18, 120), Some((900, 550)), (1, 1), false);
        assert_eq!((l.cols, l.rows, l.logical, l.buffer, l.buffer_scale), (80, 24, (896, 544), (896, 544), 1), "the scale 1 cell-grid snap");
    }
    #[test]
    fn configure_bounds_at_scale_one_are_the_original_ones() {
        let m = Metrics::new(18, 120);
        assert_eq!(m.configure_bounds(false), ((20 * 11 + 16, 6 * 22 + 16), (400 * 11 + 16, 150 * 22 + 16)));
        // The smallest and the largest hinted window of one step are acceptable at every other step.
        for a in STEPS {
            for (cols, rows) in [(MIN_COLS, MIN_ROWS), (160, 60)] {
                let size = layout(&Metrics::new(18, a), None, (cols, rows), true).logical;
                for b in STEPS {
                    let (low, high) = Metrics::new(18, b).configure_bounds(true);
                    assert!(size.0 >= low.0 && size.1 >= low.1 && size.0 <= high.0 && size.1 <= high.1, "{a} -> {b}");
                }
            }
        }
    }
    #[test]
    fn pointer_position_in_logical_units_maps_to_the_cell_at_each_scale() {
        for s in STEPS {
            let m = Metrics::new(18, s);
            let f = f64::from(s) / 120.0;
            for (col, row) in [(0, 0), (1, 0), (0, 1), (5, 3), (79, 23), (40, 12)] {
                // Device pixel centre and both inner corners of the cell, as logical positions.
                for (dx, dy) in [(m.cw as f64 / 2.0, m.ch as f64 / 2.0), (0.25, 0.25), (m.cw as f64 - 0.25, m.ch as f64 - 0.25)] {
                    let x = ((m.pad + col * m.cw) as f64 + dx) / f;
                    let y = ((m.pad + row * m.ch) as f64 + dy) / f;
                    assert_eq!(m.cell_at(x, y), (col, row), "scale {s} cell {col},{row} at {x},{y}");
                }
            }
            // In the padding and left of the surface: the first cell.
            assert_eq!(m.cell_at(0.0, 0.0), (0, 0));
            assert_eq!(m.cell_at(-5.0, -5.0), (0, 0));
        }
        // The same logical point is a different cell only as far as cell rounding differs:
        // 440 logical px is column (440 - 8) / 10.8 = 40 at every step.
        for s in STEPS {
            assert!((39..=41).contains(&Metrics::new(18, s).cell_at(440.0 + 5.4, 100.0).0), "scale {s}");
        }
        // Scale 1 is the original mapping: (x - 8) / 11, (y - 8) / 22.
        let m = Metrics::new(18, 120);
        assert_eq!(m.cell_at(8.0 + 11.0 * 7.0 + 10.9, 8.0 + 22.0 * 2.0), (7, 2));
        assert_eq!(m.cell_at(7.9, 29.9), (0, 0));
    }
    #[test]
    fn wheel_travel_to_lines_keeps_the_remainder() {
        let mut wheel = 10.0;
        assert_eq!(wheel_lines(&mut wheel), -3);
        assert_eq!(wheel, 0.0);
        let mut wheel = -25.0;
        assert_eq!(wheel_lines(&mut wheel), 6);
        assert_eq!(wheel, -5.0);
        let mut wheel = 9.0;
        assert_eq!(wheel_lines(&mut wheel), 0);
        assert_eq!(wheel, 9.0);
    }
    #[test]
    fn status_line_format() {
        let m = Metrics::new(18, 150);
        let l = layout(&m, None, (80, 24), true);
        assert_eq!(status_line(&m, &l), "TERMSCALE scale120=150 font_px=22.5 cell=14x28 grid=80x24 buffer=1140x693 logical=912x554 pad=10 buffer_scale=1");
        let m = Metrics::new(18, 240);
        assert_eq!(status_line(&m, &layout(&m, None, (80, 24), false)), "TERMSCALE scale120=240 font_px=36 cell=22x44 grid=80x24 buffer=1792x1088 logical=896x544 pad=16 buffer_scale=2");
    }
}
