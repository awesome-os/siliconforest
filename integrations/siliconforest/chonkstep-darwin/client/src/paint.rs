//! Readable ARGB8888 frames. Wayland SHM on this target is little-endian B,G,R,A.

pub const DEFAULT_W: i32 = 640;
pub const DEFAULT_H: i32 = 360;
pub const MAX_W: i32 = 1920;
pub const MAX_H: i32 = 1080;

const GLYPH_W: i32 = 5;
const GLYPH_H: i32 = 7;

pub fn draw_size(configure_w: i32, configure_h: i32) -> (i32, i32) {
    if configure_w > 0 && configure_h > 0 {
        (configure_w.min(MAX_W), configure_h.min(MAX_H))
    } else {
        (DEFAULT_W, DEFAULT_H)
    }
}

pub fn frame_bytes(width: i32, height: i32) -> Option<usize> {
    let w = u32::try_from(width).ok()?;
    let h = u32::try_from(height).ok()?;
    (w as usize).checked_mul(h as usize)?.checked_mul(4)
}

/// Paint a changing frame. `frame` selects the background so successive
/// callbacks are visibly different. The nonce and size are drawn in a 5x7 font.
pub fn paint(pixels: &mut [u8], width: i32, height: i32, nonce: &str, frame: u32) {
    let stride = width as usize * 4;
    let bg = background(frame);
    for y in 0..height as usize {
        for x in 0..width as usize {
            put(pixels, stride, x, y, bg);
        }
    }
    let ink = if frame % 2 == 0 { [0, 0, 0, 255] } else { [255, 255, 255, 255] };
    let scale = 4;
    text(pixels, stride, 16, 16, scale, ink, "NONCE");
    text(pixels, stride, 16, 16 + 8 * scale, scale, ink, nonce);
    let frame_line = format!("FRAME {frame}");
    text(pixels, stride, 16, 16 + 18 * scale, scale, ink, &frame_line);
    let size_line = format!("{width}x{height}");
    text(pixels, stride, 16, 16 + 28 * scale, scale, ink, &size_line);
}

fn background(frame: u32) -> [u8; 4] {
    match frame % 4 {
        0 => [220, 40, 40, 255],
        1 => [40, 180, 40, 255],
        2 => [40, 80, 220, 255],
        _ => [230, 230, 40, 255],
    }
}

fn put(pixels: &mut [u8], stride: usize, x: usize, y: usize, bgra: [u8; 4]) {
    let i = y * stride + x * 4;
    if i + 4 <= pixels.len() {
        pixels[i..i + 4].copy_from_slice(&bgra);
    }
}

fn text(pixels: &mut [u8], stride: usize, x: i32, y: i32, scale: i32, ink: [u8; 4], s: &str) {
    let mut cursor = x;
    for ch in s.chars() {
        glyph(pixels, stride, cursor, y, scale, ink, ch);
        cursor += (GLYPH_W + 1) * scale;
    }
}

fn glyph(pixels: &mut [u8], stride: usize, x: i32, y: i32, scale: i32, ink: [u8; 4], ch: char) {
    let rows = glyph_rows(ch);
    for row in 0..GLYPH_H {
        let bits = rows[row as usize];
        for col in 0..GLYPH_W {
            if bits & (1 << (GLYPH_W - 1 - col)) == 0 {
                continue;
            }
            for sy in 0..scale {
                for sx in 0..scale {
                    let px = x + col * scale + sx;
                    let py = y + row * scale + sy;
                    if px >= 0 && py >= 0 {
                        put(pixels, stride, px as usize, py as usize, ink);
                    }
                }
            }
        }
    }
}

fn glyph_rows(ch: char) -> [u8; 7] {
    let upper = match ch {
        'a'..='z' => (ch as u8 - b'a' + b'A') as char,
        other => other,
    };
    match upper {
        ' ' => [0, 0, 0, 0, 0, 0, 0],
        '0' => [0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E],
        '1' => [0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E],
        '2' => [0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F],
        '3' => [0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E],
        '4' => [0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02],
        '5' => [0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E],
        '6' => [0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E],
        '7' => [0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08],
        '8' => [0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E],
        '9' => [0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C],
        'A' => [0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11],
        'B' => [0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E],
        'C' => [0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E],
        'D' => [0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C],
        'E' => [0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F],
        'F' => [0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10],
        'G' => [0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F],
        'H' => [0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11],
        'I' => [0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E],
        'J' => [0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E],
        'K' => [0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11],
        'L' => [0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F],
        'M' => [0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11],
        'N' => [0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11],
        'O' => [0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E],
        'P' => [0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10],
        'Q' => [0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D],
        'R' => [0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11],
        'S' => [0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E],
        'T' => [0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04],
        'U' => [0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E],
        'V' => [0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04],
        'W' => [0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11],
        'X' => [0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11],
        'Y' => [0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04],
        'Z' => [0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F],
        _ => [0x1F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1F],
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn nonce_pixels_change_with_the_frame() {
        let bytes = frame_bytes(DEFAULT_W, DEFAULT_H).unwrap();
        let mut first = vec![0; bytes];
        let mut second = vec![0; bytes];
        paint(&mut first, DEFAULT_W, DEFAULT_H, "C369", 0);
        paint(&mut second, DEFAULT_W, DEFAULT_H, "C369", 1);
        assert_ne!(first, second);
        assert!(first.iter().any(|b| *b != 0));
        let stride = DEFAULT_W as usize * 4;
        let ink = &first[16 * stride + 16 * 4..16 * stride + 20 * 4];
        assert!(ink.iter().any(|b| *b == 0), "dark text on frame 0");
    }

    #[test]
    fn zero_configure_uses_the_default_size() {
        assert_eq!(draw_size(0, 0), (DEFAULT_W, DEFAULT_H));
        assert_eq!(draw_size(800, 600), (800, 600));
        assert_eq!(draw_size(4000, 4000), (MAX_W, MAX_H));
    }
}
