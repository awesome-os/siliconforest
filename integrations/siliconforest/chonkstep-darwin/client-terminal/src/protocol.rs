//! Qualification is observation, separate from both renderer and PTY writer.
pub const MAX_RX: usize = 1024 * 1024;
pub const MAX_TX: usize = 16384;
#[derive(Default)]
pub struct FrameState {
    pub released: bool,
    pub pending: bool,
    pub callbacks: u32,
    pub commits: u32,
    pending_rx: usize,
    pub visible_rx: usize,
}
impl FrameState {
    pub fn new() -> Self {
        Self {
            released: true,
            ..Self::default()
        }
    }
    pub fn can_draw(&self, ssd: bool) -> bool {
        ssd && self.released && !self.pending
    }
    pub fn commit(&mut self, ssd: bool, rx: usize) -> Result<(), &'static str> {
        if !self.can_draw(ssd) {
            return Err("buffer owned, frame pending, or SSD unconfigured");
        }
        self.released = false;
        self.pending = true;
        self.pending_rx = rx;
        self.commits += 1;
        Ok(())
    }
    pub fn release(&mut self) {
        self.released = true;
    }
    pub fn done(&mut self) {
        if self.pending {
            self.pending = false;
            self.callbacks += 1;
            self.visible_rx = self.pending_rx;
        }
    }
}
pub struct Proof {
    nonce: String,
    raw: Vec<u8>,
    armed_at: usize,
    pub ready: bool,
    pub response: bool,
}
impl Proof {
    pub fn new(nonce: String) -> Self {
        Self {
            nonce,
            raw: Vec::new(),
            armed_at: 0,
            ready: false,
            response: false,
        }
    }
    pub fn arm(&mut self, pty: bool, keymap: bool, focus: bool, ssd: bool, frames: u32) -> bool {
        if !self.ready && pty && keymap && focus && ssd && frames > 0 {
            self.ready = true;
            self.armed_at = self.raw.len();
            true
        } else {
            false
        }
    }
    pub fn receive(&mut self, bytes: &[u8]) -> Result<bool, &'static str> {
        if self.raw.len() + bytes.len() > MAX_RX {
            return Err("PTY output quota exceeded");
        }
        self.raw.extend_from_slice(bytes);
        if !self.ready || self.response {
            return Ok(false);
        }
        // ONLY CR is removed. ANSI escapes, stderr, blank lines, and NUL remain.
        let normalized: Vec<u8> = self.raw[self.armed_at..]
            .iter()
            .copied()
            .filter(|b| *b != b'\r')
            .collect();
        let expected = format!("\nDLT_{}\nDarwin\narm64\n25.5.0\n", self.nonce);
        if normalized
            .windows(expected.len())
            .any(|b| b == expected.as_bytes())
        {
            self.response = true;
            Ok(true)
        } else {
            Ok(false)
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn buffer_needs_ack_release_and_frame() {
        let mut f = FrameState::new();
        assert!(f.commit(false, 0).is_err());
        f.commit(true, 0).unwrap();
        assert!(f.commit(true, 3).is_err());
        f.release();
        assert!(f.commit(true, 3).is_err());
        f.done();
        assert_eq!(f.visible_rx, 0);
        assert!(f.can_draw(true));
        f.commit(true, 3).unwrap();
        assert_eq!(f.visible_rx, 0);
        f.done();
        assert_eq!(f.visible_rx, 3);
        assert!(!f.can_draw(true));
        f.release();
        assert!(f.can_draw(true));
    }
    #[test]
    fn output_before_ready_never_becomes_response() {
        let mut p = Proof::new("a".repeat(48));
        let b = format!("\nDLT_{}\nDarwin\narm64\n25.5.0\n", "a".repeat(48));
        assert!(!p.receive(b.as_bytes()).unwrap());
        p.arm(true, true, true, true, 1);
        assert!(!p.receive(b"more output").unwrap());
        assert!(p.receive(b.as_bytes()).unwrap());
    }
    #[test]
    fn ready_requires_every_actual_gate_once() {
        for missing in 0..5 {
            let mut p = Proof::new("a".repeat(48));
            let mut b = [true; 5];
            b[missing] = false;
            assert!(!p.arm(b[0], b[1], b[2], b[3], u32::from(b[4])));
            assert!(p.arm(true, true, true, true, 1));
            assert!(!p.arm(true, true, true, true, 2));
        }
    }
    #[test]
    fn strict_split_response_and_echo_is_not_output() {
        let nonce = "a".repeat(48);
        let data = format!("\r\nDLT_{nonce}\r\nDarwin\r\narm64\r\n25.5.0\r\n");
        for split in 0..=data.len() {
            let mut p = Proof::new(nonce.clone());
            p.arm(true, true, true, true, 1);
            let a = p.receive(&data.as_bytes()[..split]).unwrap();
            let b = p.receive(&data.as_bytes()[split..]).unwrap();
            assert!(a || b);
        }
        let mut p = Proof::new(nonce);
        p.arm(true, true, true, true, 1);
        assert!(!p
            .receive(b"printf '\\nDLT_%s%s\\n' aaaa aaaa; uname -s; uname -m; uname -r\r\n")
            .unwrap());
    }
    #[test]
    fn warnings_escapes_nul_wrong_release_never_filtered() {
        for extra in ["warning\n", "\x1b[0m", "\0", "\n"] {
            let mut p = Proof::new("b".repeat(48));
            p.arm(true, true, true, true, 1);
            let d = format!("\nDLT_{}\nDarwin\n{extra}arm64\n25.5.0\n", "b".repeat(48));
            assert!(!p.receive(d.as_bytes()).unwrap());
        }
        let mut p = Proof::new("b".repeat(48));
        p.arm(true, true, true, true, 1);
        assert!(!p
            .receive(format!("\nDLT_{}\nDarwin\narm64\n25.4.0\n", "b".repeat(48)).as_bytes())
            .unwrap());
    }
}
