use crate::grid::Grid;
use tiny_skia::{FillRule, Paint, PathBuilder, Pixmap, Transform};
pub const CW: usize = 11;
pub const CH: usize = 22;
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
pub struct Renderer {
    atlas: Vec<Vec<u8>>,
}
impl Renderer {
    pub fn new() -> Result<Self, &'static str> {
        let face = ttf_parser::Face::parse(include_bytes!("../fonts/IBMPlexMono-Regular.ttf"), 0)
            .map_err(|_| "font parse failed")?;
        let scale = 18.0 / f32::from(face.units_per_em());
        let mut atlas = Vec::new();
        for c in 32u8..=126 {
            let mut p = Pixmap::new(CW as u32, CH as u32).ok_or("glyph allocation")?;
            let id = face
                .glyph_index(c as char)
                .ok_or("font lacks printable ASCII")?;
            let mut outline = Outline(PathBuilder::new());
            face.outline_glyph(id, &mut outline);
            if let Some(path) = outline.0.finish() {
                let mut paint = Paint::default();
                paint.set_color_rgba8(226, 232, 238, 255);
                p.fill_path(
                    &path,
                    &paint,
                    FillRule::Winding,
                    Transform::from_row(scale, 0., 0., -scale, 0., 17.),
                    None,
                );
            }
            atlas.push(p.data().to_vec());
        }
        Ok(Self { atlas })
    }
    pub fn pixels(&self, g: &Grid) -> Vec<u8> {
        let w = g.cols * CW + PAD * 2;
        let h = g.rows * CH + PAD * 2;
        let mut out = vec![0; w * h * 4];
        for pixel in out.as_chunks_mut::<4>().0 {
            pixel.copy_from_slice(&[26, 22, 20, 255]);
        }
        for y in 0..g.rows {
            for x in 0..g.cols {
                let c = g.cells[y * g.cols + x];
                let c = if (' '..='~').contains(&c) { c } else { '?' };
                let a = &self.atlas[c as usize - 32];
                for yy in 0..CH {
                    for xx in 0..CW {
                        let src = &a[(yy * CW + xx) * 4..][..4];
                        let alpha = u32::from(src[3]);
                        let pos = ((PAD + y * CH + yy) * w + PAD + x * CW + xx) * 4;
                        for (d, s) in [(0, 2), (1, 1), (2, 0)] {
                            out[pos + d] = (u32::from(src[s])
                                + u32::from(out[pos + d]) * (255 - alpha) / 255)
                                .min(255) as u8;
                        }
                    }
                }
            }
        }
        // A plain cursor is UI state, never substitute text for PTY output.
        for yy in CH - 2..CH {
            for xx in 0..CW {
                let p = ((PAD + g.y * CH + yy) * w + PAD + g.x * CW + xx) * 4;
                out[p..p + 4].copy_from_slice(&[180, 190, 200, 255]);
            }
        }
        out
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn case_correct_real_font() {
        let r = Renderer::new().unwrap();
        assert_ne!(r.atlas[b'a' as usize - 32], r.atlas[b'A' as usize - 32]);
        for c in 33..=126 {
            assert!(r.atlas[c - 32].iter().any(|b| *b != 0), "glyph {c}");
        }
        let mut g = Grid::new(20, 6).unwrap();
        let blank = r.pixels(&g);
        g.feed(b"actual sh $> lowerCase");
        let data = r.pixels(&g);
        assert_ne!(blank, data);
        assert_eq!(data.len(), (20 * CW + 16) * (6 * CH + 16) * 4);
    }
}
