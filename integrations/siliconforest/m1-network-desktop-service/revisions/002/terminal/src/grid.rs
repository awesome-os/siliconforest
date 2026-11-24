//! Bounded TERM=dumb text grid. No shell, nonce recognition, or response generator.
#[derive(Clone, Debug)]
enum Escape {
    None,
    Esc,
    Csi(Vec<u8>),
    DiscardCsi,
    Osc { count: usize, esc: bool },
}
pub struct Grid {
    pub cols: usize,
    pub rows: usize,
    pub cells: Vec<char>,
    pub x: usize,
    pub y: usize,
    pub unsupported: u64,
    escape: Escape,
    utf8: Vec<u8>,
    wrap: bool,
}
impl Grid {
    pub fn new(cols: usize, rows: usize) -> Result<Self, &'static str> {
        if !(20..=160).contains(&cols) || !(6..=60).contains(&rows) {
            return Err("grid dimensions outside bound");
        }
        Ok(Self {
            cols,
            rows,
            cells: vec![' '; cols * rows],
            x: 0,
            y: 0,
            unsupported: 0,
            escape: Escape::None,
            utf8: Vec::new(),
            wrap: false,
        })
    }
    pub fn resize(&mut self, cols: usize, rows: usize) -> Result<(), &'static str> {
        let mut next = Self::new(cols, rows)?;
        for y in 0..rows.min(self.rows) {
            for x in 0..cols.min(self.cols) {
                next.cells[y * cols + x] = self.cells[y * self.cols + x];
            }
        }
        next.x = self.x.min(cols - 1);
        next.y = self.y.min(rows - 1);
        next.unsupported = self.unsupported;
        next.escape = self.escape.clone();
        next.utf8 = std::mem::take(&mut self.utf8);
        *self = next;
        Ok(())
    }
    fn newline(&mut self) {
        self.wrap = false;
        if self.y + 1 == self.rows {
            self.cells.copy_within(self.cols.., 0);
            self.cells[(self.rows - 1) * self.cols..].fill(' ');
        } else {
            self.y += 1;
        }
    }
    fn put(&mut self, c: char) {
        if self.wrap {
            self.x = 0;
            self.newline();
        }
        // Non-ASCII has no width policy in this first TERM=dumb client.
        let c = if c.is_ascii() {
            c
        } else {
            self.unsupported += 1;
            '\u{fffd}'
        };
        self.cells[self.y * self.cols + self.x] = c;
        if self.x + 1 == self.cols {
            self.wrap = true;
        } else {
            self.x += 1;
        }
    }
    fn csi(&mut self, p: &[u8], end: u8) {
        if p.starts_with(b"?") {
            self.unsupported += 1;
            return;
        }
        let Ok(s) = std::str::from_utf8(p) else {
            self.unsupported += 1;
            return;
        };
        let values: Option<Vec<usize>> = s
            .split(';')
            .map(|v| {
                if v.is_empty() {
                    Some(0)
                } else {
                    v.parse().ok()
                }
            })
            .collect();
        let Some(v) = values else {
            self.unsupported += 1;
            return;
        };
        if v.len() > 8 {
            self.unsupported += 1;
            return;
        }
        let n = v.first().copied().unwrap_or(0);
        let step = n.clamp(1, 1000);
        self.wrap = false;
        match end {
            b'A' => self.y = self.y.saturating_sub(step),
            b'B' => self.y = (self.y + step).min(self.rows - 1),
            b'C' => self.x = (self.x + step).min(self.cols - 1),
            b'D' => self.x = self.x.saturating_sub(step),
            b'G' => self.x = step.saturating_sub(1).min(self.cols - 1),
            b'H' | b'f' => {
                self.y = n.max(1).saturating_sub(1).min(self.rows - 1);
                self.x = v
                    .get(1)
                    .copied()
                    .unwrap_or(1)
                    .max(1)
                    .saturating_sub(1)
                    .min(self.cols - 1);
            }
            b'K' => {
                let row = self.y * self.cols;
                match n {
                    0 => self.cells[row + self.x..row + self.cols].fill(' '),
                    1 => self.cells[row..=row + self.x].fill(' '),
                    2 => self.cells[row..row + self.cols].fill(' '),
                    _ => self.unsupported += 1,
                }
            }
            b'J' => {
                let pos = self.y * self.cols + self.x;
                match n {
                    0 => self.cells[pos..].fill(' '),
                    1 => self.cells[..=pos].fill(' '),
                    2 => self.cells.fill(' '),
                    _ => self.unsupported += 1,
                }
            }
            b'm' => {
                if v.iter().any(|x| *x != 0) {
                    self.unsupported += 1;
                }
            } // monochrome, never advertise colour capability
            _ => self.unsupported += 1,
        }
    }
    pub fn feed(&mut self, bytes: &[u8]) {
        for &b in bytes {
            match std::mem::replace(&mut self.escape, Escape::None) {
                Escape::Esc => {
                    self.escape = match b {
                        b'[' => Escape::Csi(Vec::new()),
                        b']' => Escape::Osc {
                            count: 0,
                            esc: false,
                        },
                        _ => {
                            self.unsupported += 1;
                            Escape::None
                        }
                    };
                    continue;
                }
                Escape::Csi(mut p) => {
                    if (0x40..=0x7e).contains(&b) {
                        self.csi(&p, b);
                    } else if p.len() < 64 && (0x20..=0x3f).contains(&b) {
                        p.push(b);
                        self.escape = Escape::Csi(p);
                    } else {
                        self.unsupported += 1;
                        self.escape = Escape::DiscardCsi;
                    }
                    continue;
                }
                Escape::DiscardCsi => {
                    if !(0x40..=0x7e).contains(&b) {
                        self.escape = Escape::DiscardCsi;
                    }
                    continue;
                }
                Escape::Osc { count, esc } => {
                    if b == 7 || (esc && b == b'\\') {
                        self.unsupported += 1;
                    } else {
                        self.escape = Escape::Osc {
                            count: (count + 1).min(256),
                            esc: b == 27,
                        };
                    }
                    continue;
                }
                Escape::None => (),
            }
            if !self.utf8.is_empty() && !(0x80..=0xbf).contains(&b) {
                self.utf8.clear();
                self.put('\u{fffd}');
            }
            match b {
                27 => self.escape = Escape::Esc,
                b'\r' => {
                    self.x = 0;
                    self.wrap = false;
                }
                b'\n' => self.newline(),
                8 | 127 => {
                    self.x = self.x.saturating_sub(1);
                    self.wrap = false;
                }
                b'\t' => {
                    self.x = ((self.x / 8 + 1) * 8).min(self.cols - 1);
                    self.wrap = false;
                }
                12 => {
                    self.cells.fill(' ');
                    self.x = 0;
                    self.y = 0;
                    self.wrap = false;
                }
                0 | 7 => (),
                1..=31 => self.unsupported += 1,
                32..=126 => self.put(char::from(b)),
                _ => {
                    self.utf8.push(b);
                    match std::str::from_utf8(&self.utf8) {
                        Ok(s) => {
                            let c = s.chars().next().unwrap();
                            self.utf8.clear();
                            self.put(c);
                        }
                        Err(e) if e.error_len().is_none() && self.utf8.len() < 4 => (),
                        Err(_) => {
                            self.utf8.clear();
                            self.put('\u{fffd}');
                        }
                    }
                }
            }
        }
    }
    pub fn text(&self) -> String {
        self.cells
            .chunks(self.cols)
            .map(|r| r.iter().collect::<String>())
            .collect::<Vec<_>>()
            .join("\n")
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn case_punctuation_and_crlf() {
        let mut g = Grid::new(20, 6).unwrap();
        g.feed(b"aA $/#[]{}\r\nnext\r\r\nZ");
        assert!(g.text().starts_with("aA $/#[]{}"));
        assert_eq!(g.cells[20], 'n');
        assert_eq!(g.cells[40], 'Z');
    }
    #[test]
    fn split_stream_equivalent() {
        let data = b"Ab\x1b[2Dxy\r\n\xe2\x82\xac\x1b]52;ignored\x07END";
        let mut expected = Grid::new(20, 6).unwrap();
        expected.feed(data);
        for n in 0..data.len() {
            let mut g = Grid::new(20, 6).unwrap();
            g.feed(&data[..n]);
            g.feed(&data[n..]);
            assert_eq!(g.text(), expected.text());
            assert_eq!(g.unsupported, expected.unsupported);
        }
    }
    #[test]
    fn wrap_scroll_erase_resize() {
        let mut g = Grid::new(20, 6).unwrap();
        for _ in 0..130 {
            g.feed(b"a");
        }
        assert_eq!((g.x, g.y), (10, 5));
        g.feed(b"\r\x1b[KHi\tX\x08Y");
        assert_eq!(g.cells[5 * 20 + 8], 'Y');
        g.resize(40, 10).unwrap();
        assert_eq!(g.cells[5 * 40], 'H');
        assert!(g.resize(100000, 1).is_err());
    }
    #[test]
    fn hostile_controls_bounded() {
        let mut g = Grid::new(20, 6).unwrap();
        g.feed(b"\x1b[99999999999999999999999999999999H");
        g.feed(&vec![0xff; 10000]);
        g.feed(b"\x1b]");
        g.feed(&vec![b'a'; 10000]);
        assert_eq!(g.cells.len(), 120);
        assert!(g.x < 20 && g.y < 6);
        assert!(g.unsupported > 0);
    }
    #[test]
    fn overlong_escape_payload_is_discarded_until_terminator() {
        let mut g = Grid::new(20, 6).unwrap();
        g.feed(b"\x1b]52;");
        g.feed(&vec![b'x'; 4096]);
        g.feed(b"\x07actual");
        assert!(g.text().starts_with("actual"));
        g.feed(b"\r\x1b[");
        g.feed(&vec![b'1'; 4096]);
        g.feed(b"mreal");
        assert!(g.text().starts_with("realal"));
    }
}
