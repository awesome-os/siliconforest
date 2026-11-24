//! The terminal's screen state: an xterm-style emulation from the `vt100`
//! crate (cursor addressing, erase, scroll regions, SGR 16/256/true colour,
//! alternate screen, scrollback, mode tracking), wrapped with the few things
//! a terminal has to do itself: answer device queries, bound its size, and
//! move the scrollback view.
use std::num::NonZeroU16;

pub const MIN_COLS: usize = 20;
pub const MAX_COLS: usize = 400;
pub const MIN_ROWS: usize = 6;
pub const MAX_ROWS: usize = 150;
/// Lines kept above the screen.
pub const SCROLLBACK_LINES: usize = 5000;
/// What `TERM` is set to: the emulation is the xterm subset `vt100`
/// implements, with 256 and direct colour.
pub const TERM_NAME: &str = "xterm-256color";

/// Events the emulation reports back: bytes to send to the program (answers
/// to its queries) and sequences that were not understood.
#[derive(Default)]
pub struct Reports {
    replies: Vec<u8>,
    pub unsupported: u64,
}
impl vt100::Callbacks for Reports {
    fn unhandled_char(&mut self, _: &mut vt100::Screen, _: char) {
        self.unsupported += 1;
    }
    fn unhandled_escape(&mut self, _: &mut vt100::Screen, _: Option<u8>, _: Option<u8>, _: u8) {
        self.unsupported += 1;
    }
    fn unhandled_csi(&mut self, screen: &mut vt100::Screen, i1: Option<u8>, _: Option<u8>, params: &[&[u16]], c: char) {
        let first = params.first().and_then(|p| p.first()).copied().unwrap_or(0);
        match (i1, c, first) {
            // Device status: "are you there" and "where is the cursor". Full-screen
            // programs measure the screen with the second one.
            (None, 'n', 5) => self.replies.extend_from_slice(b"\x1b[0n"),
            (None, 'n', 6) => {
                let (row, col) = screen.cursor_position();
                self.replies.extend_from_slice(format!("\x1b[{};{}R", row + 1, col + 1).as_bytes());
            }
            // Primary and secondary device attributes: a VT220-class terminal.
            (None, 'c', 0) => self.replies.extend_from_slice(b"\x1b[?62;22c"),
            (Some(b'>'), 'c', 0) => self.replies.extend_from_slice(b"\x1b[>1;10;0c"),
            _ => self.unsupported += 1,
        }
    }
    fn unhandled_osc(&mut self, _: &mut vt100::Screen, _: &[&[u8]]) {
        self.unsupported += 1;
    }
}

/// One cell as the renderer needs it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Cell {
    pub c: char,
    pub fg: [u8; 3],
    pub bg: [u8; 3],
    pub bold: bool,
    pub underline: bool,
}

pub const DEFAULT_FG: [u8; 3] = [226, 232, 238];
pub const DEFAULT_BG: [u8; 3] = [20, 22, 26];

/// xterm's 256-colour palette.
pub fn palette(index: u8) -> [u8; 3] {
    const BASE: [[u8; 3]; 16] = [
        [0, 0, 0], [205, 49, 49], [13, 188, 121], [229, 229, 16], [36, 114, 200], [188, 63, 188], [17, 168, 205], [229, 229, 229],
        [102, 102, 102], [241, 76, 76], [35, 209, 139], [245, 245, 67], [59, 142, 234], [214, 112, 214], [41, 184, 219], [255, 255, 255],
    ];
    match index {
        0..=15 => BASE[index as usize],
        16..=231 => {
            let i = index - 16;
            let level = |v: u8| if v == 0 { 0 } else { 55 + 40 * v };
            [level(i / 36), level(i / 6 % 6), level(i % 6)]
        }
        _ => {
            let v = 8 + 10 * (index - 232);
            [v, v, v]
        }
    }
}

pub struct Term {
    parser: vt100::Parser<Reports>,
    pub cols: usize,
    pub rows: usize,
    /// The start of a sequence cut off at the end of the last chunk (see `feed`).
    carry: Vec<u8>,
    /// The highlighted text: from (col, row) up to and including (col, row),
    /// in view cells, in reading order.
    selection: Option<((usize, usize), (usize, usize))>,
}
impl Term {
    pub fn new(cols: usize, rows: usize) -> Result<Self, &'static str> {
        let (c, r) = Self::bounded(cols, rows)?;
        Ok(Self { parser: vt100::Parser::new_with_callbacks(r, c, SCROLLBACK_LINES, Reports::default()), cols, rows, carry: Vec::new(), selection: None })
    }
    fn bounded(cols: usize, rows: usize) -> Result<(NonZeroU16, NonZeroU16), &'static str> {
        if !(MIN_COLS..=MAX_COLS).contains(&cols) || !(MIN_ROWS..=MAX_ROWS).contains(&rows) {
            return Err("grid dimensions outside bound");
        }
        Ok((NonZeroU16::new(cols as u16).unwrap(), NonZeroU16::new(rows as u16).unwrap()))
    }
    /// Program output.
    ///
    /// The emulation knows save/restore cursor as `ESC 7` / `ESC 8` only;
    /// programs also use the ANSI.SYS spelling `CSI s` / `CSI u` (toybox's
    /// screen-size probe does). Those two exact three-byte sequences are
    /// rewritten to the ESC form here, across chunk boundaries too.
    pub fn feed(&mut self, bytes: &[u8]) {
        // The highlight marks cells, not text: new output would leave it on other words.
        if !bytes.is_empty() {
            self.selection = None;
        }
        let mut data = std::mem::take(&mut self.carry);
        data.extend_from_slice(bytes);
        let mut out = Vec::with_capacity(data.len());
        let mut i = 0;
        while i < data.len() {
            if data[i] != 0x1b {
                out.push(data[i]);
                i += 1;
                continue;
            }
            match (data.get(i + 1), data.get(i + 2)) {
                (Some(b'['), Some(b's')) => {
                    out.extend_from_slice(b"\x1b7");
                    i += 3;
                }
                (Some(b'['), Some(b'u')) => {
                    out.extend_from_slice(b"\x1b8");
                    i += 3;
                }
                // Cut off after ESC or ESC [: decide when the rest arrives.
                (None, _) | (Some(b'['), None) => {
                    self.carry = data[i..].to_vec();
                    break;
                }
                _ => {
                    out.push(0x1b);
                    i += 1;
                }
            }
        }
        self.parser.process(&out);
    }
    /// Bytes the program is waiting for (answers to its queries).
    pub fn take_replies(&mut self) -> Vec<u8> {
        std::mem::take(&mut self.parser.callbacks_mut().replies)
    }
    pub fn unsupported(&self) -> u64 {
        self.parser.callbacks().unsupported
    }
    pub fn resize(&mut self, cols: usize, rows: usize) -> Result<(), &'static str> {
        let (c, r) = Self::bounded(cols, rows)?;
        if (cols, rows) != (self.cols, self.rows) {
            self.selection = None;
            self.parser.screen_mut().set_size(r, c);
            (self.cols, self.rows) = (cols, rows);
        }
        Ok(())
    }
    fn screen(&self) -> &vt100::Screen {
        self.parser.screen()
    }
    pub fn alternate_screen(&self) -> bool {
        self.screen().alternate_screen()
    }
    pub fn application_cursor(&self) -> bool {
        self.screen().application_cursor()
    }
    pub fn bracketed_paste(&self) -> bool {
        self.screen().bracketed_paste()
    }
    /// How many lines the view is scrolled back (0: the live screen).
    pub fn scrollback(&self) -> usize {
        self.screen().scrollback()
    }
    /// Lines of history currently held above the screen.
    pub fn scrollback_len(&self) -> usize {
        self.screen().scrollback_len()
    }
    /// Move the view by `lines` (positive: back into history). Returns
    /// whether the view moved. The alternate screen has no history.
    pub fn scroll_view(&mut self, lines: isize) -> bool {
        self.selection = None;
        let now = self.scrollback();
        let wanted = (now as isize + lines).max(0) as usize;
        self.parser.screen_mut().set_scrollback(wanted);
        self.scrollback() != now
    }
    pub fn scroll_to_bottom(&mut self) -> bool {
        let moved = self.scrollback() != 0;
        self.parser.screen_mut().set_scrollback(0);
        moved
    }
    /// Highlight from one view cell to another (either order), as a stream
    /// of text: whole lines in between. `None` removes the highlight.
    /// Returns whether anything changed.
    pub fn select(&mut self, range: Option<((usize, usize), (usize, usize))>) -> bool {
        let clamp = |(col, row): (usize, usize)| (col.min(self.cols - 1), row.min(self.rows - 1));
        let range = range.map(|(a, b)| {
            let (a, b) = (clamp(a), clamp(b));
            if (a.1, a.0) <= (b.1, b.0) { (a, b) } else { (b, a) }
        });
        let changed = range != self.selection;
        self.selection = range;
        changed
    }
    fn selected(&self, col: usize, row: usize) -> bool {
        self.selection.is_some_and(|(from, to)| (from.1, from.0) <= (row, col) && (row, col) <= (to.1, to.0))
    }
    /// The highlighted text: trailing blanks of each line dropped, lines joined by newlines.
    pub fn selected_text(&self) -> Option<String> {
        let (from, to) = self.selection?;
        let mut lines = Vec::new();
        for row in from.1..=to.1 {
            let first = if row == from.1 { from.0 } else { 0 };
            let last = if row == to.1 { to.0 } else { self.cols - 1 };
            let mut line = String::new();
            for col in first..=last {
                if let Some(cell) = self.screen().cell(row as u16, col as u16) {
                    if cell.is_wide_continuation() {
                        continue;
                    }
                    let text = cell.contents();
                    line.push_str(if text.is_empty() { " " } else { text });
                }
            }
            lines.push(line.trim_end().to_string());
        }
        Some(lines.join("\n"))
    }
    /// Cursor (column, row) on the live screen, if it is to be drawn: not
    /// hidden by the program and the view is not scrolled back.
    pub fn cursor(&self) -> Option<(usize, usize)> {
        let s = self.screen();
        if s.hide_cursor() || s.scrollback() != 0 {
            return None;
        }
        let (row, col) = s.cursor_position();
        Some((col as usize, row as usize))
    }
    /// The cell at a view position, with its colours resolved.
    pub fn cell(&self, col: usize, row: usize) -> Cell {
        let blank = Cell { c: ' ', fg: DEFAULT_FG, bg: DEFAULT_BG, bold: false, underline: false };
        let Some(cell) = self.screen().cell(row as u16, col as u16) else {
            return blank;
        };
        let colour = |c: vt100::Color, default: [u8; 3], bright: bool| match c {
            vt100::Color::Default => default,
            // Bold draws the eight basic colours in their bright variants, as xterm does.
            vt100::Color::Idx(i) if bright && i < 8 => palette(i + 8),
            vt100::Color::Idx(i) => palette(i),
            vt100::Color::Rgb(r, g, b) => [r, g, b],
        };
        let (mut fg, mut bg) = (colour(cell.fgcolor(), DEFAULT_FG, cell.bold()), colour(cell.bgcolor(), DEFAULT_BG, false));
        if cell.inverse() {
            std::mem::swap(&mut fg, &mut bg);
        }
        if cell.dim() {
            fg = fg.map(|v| (u16::from(v) * 2 / 3) as u8);
        }
        // Selected text is shown with its colours exchanged.
        if self.selected(col, row) {
            std::mem::swap(&mut fg, &mut bg);
        }
        Cell {
            c: if cell.is_wide_continuation() { ' ' } else { cell.contents().chars().next().unwrap_or(' ') },
            fg,
            bg,
            bold: cell.bold(),
            underline: cell.underline(),
        }
    }
    /// The view as text, one line per row, trailing blanks removed.
    pub fn text(&self) -> String {
        self.screen().rows(0, self.cols as u16).collect::<Vec<_>>().join("\n")
    }
    /// One line describing the state, for the live state file.
    pub fn state_line(&self) -> String {
        let (col, row) = (self.screen().cursor_position().1, self.screen().cursor_position().0);
        format!(
            "TERMSTATE cols={} rows={} alternate_screen={} application_cursor={} bracketed_paste={} cursor={},{} cursor_hidden={} scrollback_offset={} scrollback_lines={} unsupported={}",
            self.cols,
            self.rows,
            u8::from(self.alternate_screen()),
            u8::from(self.application_cursor()),
            u8::from(self.bracketed_paste()),
            col,
            row,
            u8::from(self.screen().hide_cursor()),
            self.scrollback(),
            self.scrollback_len(),
            self.unsupported(),
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn term() -> Term {
        Term::new(40, 10).unwrap()
    }
    fn row(t: &Term, y: usize) -> String {
        (0..t.cols).map(|x| t.cell(x, y).c).collect::<String>().trim_end().to_string()
    }

    #[test]
    fn bounds_and_plain_text() {
        assert!(Term::new(19, 10).is_err() && Term::new(40, 5).is_err() && Term::new(401, 10).is_err());
        let mut t = term();
        t.feed(b"hello\r\nworld");
        assert_eq!((row(&t, 0).as_str(), row(&t, 1).as_str()), ("hello", "world"));
        assert_eq!(t.cursor(), Some((5, 1)));
        assert_eq!(t.text().lines().next(), Some("hello"));
    }

    #[test]
    fn cursor_addressing_erase_and_scroll_region() {
        let mut t = term();
        t.feed(b"\x1b[2J\x1b[5;10Hx\x1b[1;1Htop\x1b[10;1Hbottom");
        assert_eq!(t.cell(9, 4).c, 'x');
        assert_eq!((row(&t, 0).as_str(), row(&t, 9).as_str()), ("top", "bottom"));
        // Erase to end of line and insert/delete within a scroll region.
        t.feed(b"\x1b[1;2H\x1b[K");
        assert_eq!(row(&t, 0), "t");
        t.feed(b"\x1b[2;4r\x1b[2;1Ha\r\nb\r\nc\r\nd");
        // Rows 2..=4 scrolled once: a left the region, the first and last rows did not move.
        assert_eq!((row(&t, 1).as_str(), row(&t, 2).as_str(), row(&t, 3).as_str()), ("b", "c", "d"));
        assert_eq!((row(&t, 0).as_str(), row(&t, 9).as_str()), ("t", "bottom"));
    }

    #[test]
    fn sgr_sixteen_256_and_true_colour_bold_inverse() {
        let mut t = term();
        t.feed(b"\x1b[31mR\x1b[1;32mG\x1b[0;38;5;196mX\x1b[38;2;1;2;3;48;2;9;8;7mT\x1b[0;7mI\x1b[0;4mU\x1b[0mn");
        assert_eq!(t.cell(0, 0).fg, palette(1));
        assert_eq!((t.cell(1, 0).fg, t.cell(1, 0).bold), (palette(10), true)); // bold green is bright green
        assert_eq!(t.cell(2, 0).fg, [255, 0, 0]);
        assert_eq!((t.cell(3, 0).fg, t.cell(3, 0).bg), ([1, 2, 3], [9, 8, 7]));
        assert_eq!((t.cell(4, 0).fg, t.cell(4, 0).bg), (DEFAULT_BG, DEFAULT_FG));
        assert!(t.cell(5, 0).underline);
        assert_eq!((t.cell(6, 0).fg, t.cell(6, 0).bg), (DEFAULT_FG, DEFAULT_BG));
        assert_eq!((palette(16), palette(231), palette(232), palette(255)), ([0, 0, 0], [255, 255, 255], [8, 8, 8], [238, 238, 238]));
    }

    #[test]
    fn alternate_screen_modes_and_restore() {
        let mut t = term();
        t.feed(b"prompt$ ");
        t.feed(b"\x1b[?1049h\x1b[2J\x1b[1;1Hfull screen\x1b[?1h\x1b[?2004h\x1b[?25l");
        assert!(t.alternate_screen() && t.application_cursor() && t.bracketed_paste());
        assert_eq!(row(&t, 0), "full screen");
        assert_eq!(t.cursor(), None);
        assert!(!t.scroll_view(3), "the alternate screen has no history");
        t.feed(b"\x1b[?1l\x1b[?2004l\x1b[?25h\x1b[?1049l");
        assert!(!t.alternate_screen() && !t.application_cursor() && !t.bracketed_paste());
        assert_eq!(row(&t, 0), "prompt$");
        assert_eq!(t.cursor(), Some((8, 0)));
    }

    #[test]
    fn device_queries_are_answered() {
        let mut t = term();
        t.feed(b"\x1b[3;7H\x1b[6n");
        assert_eq!(t.take_replies(), b"\x1b[3;7R");
        // The size probe full-screen programs use: go far, ask where that is.
        t.feed(b"\x1b[s\x1b[999C\x1b[999B\x1b[6n\x1b[u");
        assert_eq!(t.take_replies(), b"\x1b[10;40R");
        assert_eq!(t.cursor(), Some((6, 2)), "the probe put the cursor back");
        // The same probe arriving one byte at a time.
        t.feed(b"\x1b[4;9H");
        for b in b"\x1b[s\x1b[999C\x1b[999B\x1b[6n\x1b[u" {
            t.feed(&[*b]);
        }
        assert_eq!((t.take_replies(), t.cursor()), (b"\x1b[10;40R".to_vec(), Some((8, 3))));
        // CSI with parameters before s is something else and is left alone.
        t.feed(b"\x1b[1;5s");
        t.feed(b"\x1b[5n\x1b[c\x1b[>c");
        assert_eq!(t.take_replies(), b"\x1b[0n\x1b[?62;22c\x1b[>1;10;0c");
        assert!(t.take_replies().is_empty());
        assert_eq!(t.unsupported(), 1, "only the parameterised CSI s above");
    }

    #[test]
    fn scrollback_keeps_history_and_the_view_moves() {
        let mut t = term();
        for i in 0..100 {
            t.feed(format!("line{i}\r\n").as_bytes());
        }
        assert_eq!(row(&t, 8), "line99");
        assert_eq!(t.scrollback_len(), 91);
        assert!(t.scroll_view(20));
        assert_eq!((t.scrollback(), row(&t, 8).as_str()), (20, "line79"));
        assert_eq!(t.cursor(), None, "no cursor while looking at history");
        assert!(t.scroll_view(1000));
        assert_eq!((t.scrollback(), row(&t, 0).as_str()), (91, "line0"));
        assert!(!t.scroll_view(5), "already at the top");
        assert!(t.scroll_view(-1000) && t.scrollback() == 0);
        assert!(!t.scroll_to_bottom());
        assert!(t.state_line().contains("scrollback_offset=0 scrollback_lines=91"));
    }

    #[test]
    fn resize_keeps_content_and_reports_the_new_size() {
        let mut t = term();
        t.feed(b"keep me");
        t.resize(60, 20).unwrap();
        assert_eq!((t.cols, t.rows, row(&t, 0).as_str()), (60, 20, "keep me"));
        t.feed(b"\x1b[999;999H\x1b[6n");
        assert_eq!(t.take_replies(), b"\x1b[20;60R");
        assert!(t.resize(10, 20).is_err());
    }

    #[test]
    fn selection_is_a_stream_of_text_shown_inverted_and_dropped_by_output() {
        let mut t = term();
        t.feed(b"alpha beta\r\ngamma\r\ndelta epsilon");
        // Backwards drag: from the "ep" of the third line up to the "b" of the first.
        assert!(t.select(Some(((7, 2), (6, 0)))));
        assert_eq!(t.selected_text().unwrap(), "beta\ngamma\ndelta ep");
        assert_eq!(t.cell(6, 0).bg, DEFAULT_FG);
        assert_eq!(t.cell(5, 0).bg, DEFAULT_BG);
        assert_eq!(t.cell(39, 1).bg, DEFAULT_FG);
        assert_eq!(t.cell(8, 2).bg, DEFAULT_BG);
        // One word on one line; cells outside the grid are clamped.
        t.select(Some(((0, 1), (99, 1))));
        assert_eq!(t.selected_text().unwrap(), "gamma");
        assert!(!t.select(Some(((0, 1), (99, 1)))));
        t.feed(b"!");
        assert_eq!(t.selected_text(), None);
        assert_eq!(t.cell(0, 1).bg, DEFAULT_BG);
    }
}
