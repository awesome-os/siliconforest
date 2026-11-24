//! Bounded application transcript and SHM frame ownership; no response recognizer.
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
}
