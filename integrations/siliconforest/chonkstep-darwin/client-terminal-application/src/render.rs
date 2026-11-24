//! CPU rendering of the terminal view into an XRGB8888 window buffer: cell
//! backgrounds, glyphs tinted with the cell's foreground, underline and the
//! cursor. Only rows whose content changed are redrawn; the caller writes and
//! damages just those. Everything is drawn at the device-pixel metrics of the
//! current output scale: glyph outlines are rasterised at the real pixel size,
//! never drawn small and enlarged.
use crate::scale::Metrics;
use crate::term::{Cell, Term, DEFAULT_BG};
use std::collections::HashMap;
use tiny_skia::{FillRule, Paint, PathBuilder, Pixmap, Transform};
/// The cell and padding at scale 1 with the default font size.
#[cfg(test)]
pub const CW: usize = 11;
#[cfg(test)]
pub const CH: usize = 22;
#[cfg(test)]
pub const PAD: usize = 8;
struct Outline(PathBuilder);
impl ttf_parser::OutlineBuilder for Outline {
    fn move_to(&mut self, x: f32, y: f32) {
        self.0.move_to(x, y)
    }
    fn line_to(&mut self, x: f32, y: f32) {
        self.0.line_to(x, y)
    }
    fn quad_to(&mut self, x1: f32, y1: f32, x: f32, y: f32) {
        self.0.quad_to(x1, y1, x, y)
    }
    fn curve_to(&mut self, x1: f32, y1: f32, x2: f32, y2: f32, x: f32, y: f32) {
        self.0.cubic_to(x1, y1, x2, y2, x, y)
    }
    fn close(&mut self) {
        self.0.close()
    }
}
/// What one row showed when it was last drawn: its cells and the cursor column on it.
type RowKey = (Vec<Cell>, Option<usize>);
pub struct Renderer {
    face: ttf_parser::Face<'static>,
    /// Cell metrics in device pixels at the current scale.
    m: Metrics,
    /// Coverage masks (cw x ch, one byte per pixel); `None`: the font has no such glyph.
    glyphs: HashMap<char, Option<Vec<u8>>>,
    /// Rows as last drawn into the caller's buffer, and that buffer's size.
    shown: Vec<RowKey>,
    shown_size: (usize, usize),
}
impl Renderer {
    pub fn new() -> Result<Self, &'static str> {
        let face = ttf_parser::Face::parse(include_bytes!("../fonts/IBMPlexMono-Regular.ttf"), 0)
            .map_err(|_| "font parse failed")?;
        // The cell width is computed from these; another font needs its own.
        let advance = face.glyph_index('M').and_then(|id| face.glyph_hor_advance(id));
        if u32::from(face.units_per_em()) != crate::scale::UNITS_PER_EM || advance.map(u32::from) != Some(crate::scale::ADVANCE_UNITS) {
            return Err("font metrics differ from the cell metrics");
        }
        let m = Metrics::new(crate::scale::BASE_FONT_PX, crate::scale::UNIT);
        let mut renderer = Self { face, m, glyphs: HashMap::new(), shown: Vec::new(), shown_size: (0, 0) };
        for c in 33u8..=126 {
            if renderer.glyph(c as char).is_none() {
                return Err("font lacks printable ASCII");
            }
        }
        Ok(renderer)
    }
    /// Draw at these metrics from now on: glyphs are rasterised again at the
    /// new pixel size and everything is redrawn.
    pub fn set_metrics(&mut self, m: Metrics) {
        if self.m != m {
            self.m = m;
            self.glyphs.clear();
            self.invalidate();
        }
    }
    /// The coverage mask of `c`, rasterised on first use at the device pixel size.
    fn glyph(&mut self, c: char) -> Option<&[u8]> {
        if !self.glyphs.contains_key(&c) {
            let m = self.m;
            let mask = self.face.glyph_index(c).and_then(|id| {
                let scale = m.font_px() as f32 / f32::from(self.face.units_per_em());
                let mut outline = Outline(PathBuilder::new());
                self.face.outline_glyph(id, &mut outline);
                let mut p = Pixmap::new(m.cw as u32, m.ch as u32)?;
                if let Some(path) = outline.0.finish() {
                    let mut paint = Paint::default();
                    paint.set_color_rgba8(255, 255, 255, 255);
                    p.fill_path(&path, &paint, FillRule::Winding, Transform::from_row(scale, 0., 0., -scale, 0., m.baseline as f32), None);
                }
                Some(p.data().chunks_exact(4).map(|px| px[3]).collect())
            });
            // Bound the cache: a program printing every code point cannot grow it without limit.
            if self.glyphs.len() >= 4096 {
                self.glyphs.retain(|c, _| c.is_ascii());
            }
            self.glyphs.insert(c, mask);
        }
        self.glyphs.get(&c).and_then(|m| m.as_deref())
    }
    /// Forget what is in the buffer (it is new or was resized): draw everything.
    pub fn invalidate(&mut self) {
        self.shown.clear();
    }
    /// Draw the view into `out` (`width` x `height` pixels, XRGB8888 little
    /// endian: B, G, R, x). Returns the pixel rows `[y0, y1)` that changed,
    /// or `None` when nothing did.
    pub fn draw(&mut self, t: &Term, out: &mut [u8], width: usize, height: usize) -> Option<(usize, usize)> {
        assert_eq!(out.len(), width * height * 4);
        let put = |out: &mut [u8], x: usize, y: usize, rgb: [u8; 3]| {
            if x < width && y < height {
                out[(y * width + x) * 4..][..4].copy_from_slice(&[rgb[2], rgb[1], rgb[0], 255]);
            }
        };
        let mut changed: Option<(usize, usize)> = None;
        if self.shown_size != (width, height) || self.shown.len() != t.rows {
            for px in out.chunks_exact_mut(4) {
                px.copy_from_slice(&[DEFAULT_BG[2], DEFAULT_BG[1], DEFAULT_BG[0], 255]);
            }
            self.shown = vec![(Vec::new(), None); t.rows];
            self.shown_size = (width, height);
            changed = Some((0, height));
        }
        let cursor = t.cursor();
        let Metrics { cw, ch, pad, stroke, .. } = self.m;
        // The missing-glyph box and the underline keep their proportions of the em.
        let (box_left, box_top, box_bottom, under) = (self.m.em18(2), self.m.em18(5), self.m.em18(17), self.m.em18(3));
        for row in 0..t.rows {
            let cells: Vec<Cell> = (0..t.cols).map(|col| t.cell(col, row)).collect();
            let key: RowKey = (cells, cursor.filter(|c| c.1 == row).map(|c| c.0));
            if self.shown[row] == key {
                continue;
            }
            let y0 = pad + row * ch;
            for (col, cell) in key.0.iter().enumerate() {
                let x0 = pad + col * cw;
                // The cursor is a block: the cell's colours exchanged.
                let (fg, bg) = if key.1 == Some(col) { (cell.bg, cell.fg) } else { (cell.fg, cell.bg) };
                for yy in 0..ch {
                    for xx in 0..cw {
                        put(out, x0 + xx, y0 + yy, bg);
                    }
                }
                if cell.c != ' ' {
                    // A glyph the font lacks is shown as a hollow box, not as another character.
                    let mask: Vec<u8> = match self.glyph(cell.c) {
                        Some(mask) => mask.to_vec(),
                        None => (0..cw * ch)
                            .map(|i| {
                                let (x, y) = (i % cw, i / cw);
                                let box_right = (cw - 1).saturating_sub(box_left);
                                let near = |v: usize, low: usize, high: usize| (low..low + stroke).contains(&v) || v <= high && v + stroke > high;
                                let edge = near(x, box_left, box_right) && (box_top..=box_bottom).contains(&y) || near(y, box_top, box_bottom) && (box_left..=box_right).contains(&x);
                                if edge { 255 } else { 0 }
                            })
                            .collect(),
                    };
                    for yy in 0..ch {
                        for xx in 0..cw {
                            // Bold: the same outline again, a stroke (one pixel at scale 1) to the right.
                            let mut alpha = u32::from(mask[yy * cw + xx]);
                            if cell.bold {
                                for d in 1..=stroke.min(xx) {
                                    alpha = alpha.max(u32::from(mask[yy * cw + xx - d]));
                                }
                            }
                            if alpha != 0 {
                                let mix = |f: u8, b: u8| ((u32::from(f) * alpha + u32::from(b) * (255 - alpha)) / 255) as u8;
                                put(out, x0 + xx, y0 + yy, [mix(fg[0], bg[0]), mix(fg[1], bg[1]), mix(fg[2], bg[2])]);
                            }
                        }
                    }
                }
                if cell.underline {
                    for yy in ch.saturating_sub(under)..(ch.saturating_sub(under) + stroke).min(ch) {
                        for xx in 0..cw {
                            put(out, x0 + xx, y0 + yy, fg);
                        }
                    }
                }
            }
            self.shown[row] = key;
            let (top, bottom) = (y0.min(height), (y0 + ch).min(height));
            changed = Some(match changed {
                Some((a, b)) => (a.min(top), b.max(bottom)),
                None => (top, bottom),
            });
        }
        changed
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    fn pixel(out: &[u8], width: usize, x: usize, y: usize) -> [u8; 3] {
        let p = &out[(y * width + x) * 4..][..4];
        [p[2], p[1], p[0]]
    }
    #[test]
    fn real_font_colours_cursor_and_row_damage() {
        let mut r = Renderer::new().unwrap();
        assert_ne!(r.glyph('a').unwrap().to_vec(), r.glyph('A').unwrap().to_vec());
        for c in 33u8..=126 {
            assert!(r.glyph(c as char).unwrap().iter().any(|b| *b != 0), "glyph {c}");
        }
        let mut t = Term::new(20, 6).unwrap();
        let (w, h) = (20 * CW + 2 * PAD, 6 * CH + 2 * PAD);
        let mut out = vec![0u8; w * h * 4];
        // First draw: everything; the cursor block sits at the top left.
        assert_eq!(r.draw(&t, &mut out, w, h), Some((0, h)));
        assert_eq!(pixel(&out, w, PAD + 3, PAD + 3), crate::term::DEFAULT_FG);
        assert_eq!(pixel(&out, w, PAD + CW + 3, PAD + 3), DEFAULT_BG);
        assert_eq!(r.draw(&t, &mut out, w, h), None, "nothing changed");
        // Red text on a green background on the third row only.
        t.feed(b"\x1b[3;1H\x1b[31;42mMM\x1b[0m");
        assert_eq!(r.draw(&t, &mut out, w, h), Some((PAD, PAD + 3 * CH)), "rows 0 (cursor left) to 2");
        let row = PAD + 2 * CH;
        let cell: Vec<[u8; 3]> = (0..CH).flat_map(|yy| (0..CW).map(move |xx| (xx, yy))).map(|(xx, yy)| pixel(&out, w, PAD + xx, row + yy)).collect();
        assert!(cell.contains(&crate::term::palette(2)), "background");
        assert!(cell.contains(&crate::term::palette(1)), "glyph ink");
        assert_eq!(pixel(&out, w, PAD + 5 * CW + 3, row + 3), DEFAULT_BG, "beyond the text");
        // A character the font does not have is a box, and never panics.
        t.feed("\u{4e2d}".as_bytes());
        assert!(r.draw(&t, &mut out, w, h).is_some());
        // A new buffer size redraws everything.
        let mut bigger = vec![0u8; (w + 40) * (h + 40) * 4];
        assert_eq!(r.draw(&t, &mut bigger, w + 40, h + 40), Some((0, h + 40)));
    }
    /// A single-channel image: enlarge it by picking the nearest source pixel, or bilinearly.
    fn enlarge(src: &[u8], (sw, sh): (usize, usize), (dw, dh): (usize, usize), bilinear: bool) -> Vec<u8> {
        let at = |x: usize, y: usize| f64::from(src[y.min(sh - 1) * sw + x.min(sw - 1)]);
        (0..dw * dh)
            .map(|i| {
                let (fx, fy) = (((i % dw) as f64 + 0.5) * sw as f64 / dw as f64 - 0.5, ((i / dw) as f64 + 0.5) * sh as f64 / dh as f64 - 0.5);
                if !bilinear {
                    return at((fx + 0.5).max(0.0) as usize, (fy + 0.5).max(0.0) as usize) as u8;
                }
                let (fx, fy) = (fx.max(0.0), fy.max(0.0));
                let (x, y, tx, ty) = (fx as usize, fy as usize, fx.fract(), fy.fract());
                let top = at(x, y) * (1.0 - tx) + at(x + 1, y) * tx;
                let bottom = at(x, y + 1) * (1.0 - tx) + at(x + 1, y + 1) * tx;
                (top * (1.0 - ty) + bottom * ty).round() as u8
            })
            .collect()
    }
    /// Pixels that are neither background nor full ink, and the length of the
    /// ink's outline (neighbouring pixels on different sides of half coverage).
    fn edge_stats(mask: &[u8], (w, h): (usize, usize)) -> (usize, usize) {
        let grey = mask.iter().filter(|a| (32..224).contains(*a)).count();
        let ink = |x: usize, y: usize| mask[y * w + x] >= 128;
        let mut outline = 0;
        for y in 0..h {
            for x in 0..w {
                outline += usize::from(x + 1 < w && ink(x, y) != ink(x + 1, y)) + usize::from(y + 1 < h && ink(x, y) != ink(x, y + 1));
            }
        }
        (grey, outline)
    }
    /// Renders one line at every scale step and measures that it is drawn at
    /// the device pixel size. With `CHONK_SCALE_PROOF_DIR` set, the images
    /// (binary PPM) and the numbers are also written there.
    #[test]
    fn text_is_rasterised_at_device_pixels_at_every_scale_not_enlarged() {
        const TEXT: &str = "Hamburgefonstiv 0123456789 HHH";
        let proof = std::env::var_os("CHONK_SCALE_PROOF_DIR").map(std::path::PathBuf::from);
        let ppm = |name: String, rgb: Vec<u8>, w: usize, h: usize| {
            if let Some(dir) = &proof {
                let mut bytes = format!("P6\n{w} {h}\n255\n").into_bytes();
                bytes.extend(rgb);
                std::fs::write(dir.join(name), bytes).unwrap();
            }
        };
        // The line as the window shows it: light text on the dark background, no cursor.
        let line = |r: &mut Renderer, m: Metrics| {
            r.set_metrics(m);
            r.invalidate();
            let mut t = Term::new(TEXT.len(), 6).unwrap();
            t.feed(b"\x1b[?25l");
            t.feed(TEXT.as_bytes());
            let (w, h) = (m.grid_px(TEXT.len(), 1).0, m.ch + 2 * m.pad);
            let mut out = vec![0u8; w * h * 4];
            r.draw(&t, &mut out, w, h).unwrap();
            // Ink coverage 0..=255 from the red channel, which runs from background to foreground.
            let (bg, fg) = (f64::from(DEFAULT_BG[0]), f64::from(crate::term::DEFAULT_FG[0]));
            let coverage: Vec<u8> = out.chunks_exact(4).map(|px| ((f64::from(px[2]) - bg) / (fg - bg) * 255.0).round().clamp(0.0, 255.0) as u8).collect();
            let rgb: Vec<u8> = out.chunks_exact(4).flat_map(|px| [px[2], px[1], px[0]]).collect();
            (coverage, rgb, w, h)
        };
        let mut r = Renderer::new().unwrap();
        let base = Metrics::new(18, 120);
        let (base_cover, _, bw, bh) = line(&mut r, base);
        let h_rows = |r: &mut Renderer| {
            let cw = r.m.cw;
            let mask = r.glyph('H').unwrap().to_vec();
            mask.chunks_exact(cw).filter(|row| row.iter().any(|a| *a >= 128)).count()
        };
        let base_h = h_rows(&mut r);
        let cap = f64::from(r.face.capital_height().unwrap()) / f64::from(r.face.units_per_em());
        let mut report = format!("text: {TEXT:?}; font IBM Plex Mono, 18 logical px; cap height {cap} em; 'H' ink at scale 1: {base_h} rows\n");
        report += "scale120 font_px cell  H_ink_rows expected(cap*font_px) | grey/outline: native nearest bilinear | pixels differing from enlarged 1x: nearest bilinear\n";
        let (mut native_ratios, mut worst) = (Vec::new(), (0.0f64, 0.0f64));
        for s in [120u32, 150, 180, 210, 240, 300, 360] {
            let m = Metrics::new(18, s);
            let (cover, rgb, w, h) = line(&mut r, m);
            // (c) The cell is whole device pixels and every mask is exactly one cell.
            assert_eq!(r.glyph('H').unwrap().len(), m.cw * m.ch);
            assert_eq!((w, h), (TEXT.len() * m.cw + 2 * m.pad, m.ch + 2 * m.pad));
            // (a) The ink of 'H' is as high as the font says at this pixel size.
            let rows = h_rows(&mut r);
            let expected = cap * m.font_px();
            assert!((rows as f64 - expected).abs() <= 1.0, "scale {s}: H is {rows} rows, font says {expected}");
            assert!((rows as f64 - base_h as f64 * f64::from(s) / 120.0).abs() <= 1.0 + f64::from(s) / 240.0, "scale {s}: {rows} rows against {base_h} at scale 1");
            // (b) Not the scale-1 picture enlarged: the soft edge stays one
            // pixel wide however long the outline gets.
            let nearest = enlarge(&base_cover, (bw, bh), (w, h), false);
            let bilinear = enlarge(&base_cover, (bw, bh), (w, h), true);
            let ratio = |image: &[u8]| {
                let (grey, outline) = edge_stats(image, (w, h));
                grey as f64 / outline as f64
            };
            let (native, near, bili) = (ratio(&cover), ratio(&nearest), ratio(&bilinear));
            let differ = |other: &[u8]| cover.iter().zip(other).filter(|(a, b)| a.abs_diff(**b) > 16).count() as f64 / cover.len() as f64;
            let (d_near, d_bili) = (differ(&nearest), differ(&bilinear));
            report += &format!("{s:>8} {:>7} {:>2}x{:<2} {rows:>10} {expected:>21.2} | {native:>20.3} {near:>7.3} {bili:>8.3} | {d_near:>41.4} {d_bili:>8.4}\n", m.font_px(), m.cw, m.ch);
            native_ratios.push(native);
            if s > 120 {
                assert!(cover != nearest && cover != bilinear, "scale {s}: identical to the enlarged scale-1 picture");
                assert!(d_near > 0.01 && d_bili > 0.01, "scale {s}: {d_near} {d_bili}");
                worst = (worst.0.max(native / near), worst.1.max(native / bili));
                // The enlarged pictures have clearly more soft pixels per outline pixel.
                assert!(near > native * 1.2 && bili > native * 1.5, "scale {s}: native {native}, nearest {near}, bilinear {bili}");
            }
            let grey = |image: Vec<u8>| image.into_iter().flat_map(|a| [a, a, a]).collect::<Vec<u8>>();
            ppm(format!("text-scale{s}-native.ppm"), rgb, w, h);
            ppm(format!("coverage-scale{s}-native.ppm"), grey(cover), w, h);
            ppm(format!("coverage-scale{s}-nearest-from-1x.ppm"), grey(nearest), w, h);
            ppm(format!("coverage-scale{s}-bilinear-from-1x.ppm"), grey(bilinear), w, h);
        }
        let (low, high) = native_ratios.iter().fold((f64::MAX, 0.0f64), |(l, h), v| (l.min(*v), h.max(*v)));
        report += &format!("native grey/outline over all scales: {low:.3}..{high:.3}; largest native/nearest {:.3}, native/bilinear {:.3} (scales above 1)\n", worst.0, worst.1);
        println!("{report}");
        if let Some(dir) = &proof {
            std::fs::write(dir.join("sharpness.txt"), &report).unwrap();
        }
        // Bounded as the scale grows; an enlarged picture's ratio grows with the scale.
        assert!(high <= 1.0, "soft pixels per outline pixel: {native_ratios:?}");
        assert!(high <= native_ratios[0] * 1.25, "native ratio grows with the scale: {native_ratios:?}");
    }
}
