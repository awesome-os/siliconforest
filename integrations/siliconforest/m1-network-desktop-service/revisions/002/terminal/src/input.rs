//! Focus-local Wayland input. Accepted bytes survive Leave; releases produce none.
use crate::{keys::Keys, protocol::MAX_TX};
use std::{
    collections::{BTreeSet, VecDeque},
    io,
};

pub struct Input {
    pub keys: Keys,
    held: BTreeSet<u32>,
    pending: VecDeque<u8>,
    pub accepted: usize,
    pub written: usize,
}
impl Input {
    pub fn new() -> Self {
        Self {
            keys: Keys::new(),
            held: BTreeSet::new(),
            pending: VecDeque::new(),
            accepted: 0,
            written: 0,
        }
    }
    pub fn enter(&mut self, bytes: &[u8]) -> Result<(), &'static str> {
        if bytes.len() % 4 != 0 || bytes.len() > 128 {
            return Err("invalid held-key array");
        }
        let mut held = BTreeSet::new();
        for bytes in bytes.as_chunks::<4>().0 {
            let key = u32::from_ne_bytes(*bytes);
            if key > 0x2ff || !held.insert(key) {
                return Err("invalid held key");
            }
        }
        self.held = held;
        self.keys.focused = true;
        Ok(())
    }
    pub fn leave(&mut self) {
        self.keys.focused = false;
        self.held.clear();
        // Already accepted bytes belong to this PTY, independently of new focus.
    }
    pub fn key(&mut self, key: u32, down: bool) -> Result<(), &'static str> {
        if key > 0x2ff {
            return Err("out of range Wayland key");
        }
        if !self.keys.focused {
            return Ok(());
        }
        if down {
            if !self.held.insert(key) {
                return Err("duplicate key press");
            }
        } else {
            // A release may belong to an earlier focus or compositor-owned chord.
            self.held.remove(&key);
        }
        let bytes = self.keys.press(key, down)?;
        if self.accepted + bytes.len() > MAX_TX {
            return Err("input quota exceeded");
        }
        self.accepted += bytes.len();
        self.pending.extend(bytes);
        Ok(())
    }
    pub fn pending_len(&self) -> usize {
        self.pending.len()
    }
    pub fn write_pending(
        &mut self,
        write: impl FnOnce(&[u8]) -> io::Result<usize>,
        mut transcript: impl io::Write,
    ) -> io::Result<()> {
        if self.pending.is_empty() {
            return Ok(());
        }
        let bytes: Vec<u8> = self.pending.iter().copied().take(512).collect();
        match write(&bytes) {
            Ok(0) => Err(io::Error::other("zero PTY write")),
            Ok(n) if n <= bytes.len() => {
                // Retire actual writes even if recording later fails, so final
                // accepted/written/unsent accounting never replays these bytes.
                self.pending.drain(..n);
                self.written += n;
                transcript.write_all(&bytes[..n])
            }
            Ok(_) => Err(io::Error::other("invalid PTY write count")),
            Err(e)
                if matches!(
                    e.kind(),
                    io::ErrorKind::WouldBlock | io::ErrorKind::Interrupted
                ) =>
            {
                Ok(())
            }
            Err(e) => Err(e),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn input() -> Input {
        let c = xkbcommon::xkb::Context::new(xkbcommon::xkb::CONTEXT_NO_FLAGS);
        let m = xkbcommon::xkb::Keymap::new_from_names(
            &c,
            "evdev",
            "pc105",
            "us",
            "",
            None,
            xkbcommon::xkb::KEYMAP_COMPILE_NO_FLAGS,
        )
        .expect("host xkeyboard-config fixture");
        let mut bytes = m
            .get_as_string(xkbcommon::xkb::KEYMAP_FORMAT_TEXT_V1)
            .into_bytes();
        bytes.push(0);
        let mut input = Input::new();
        input.keys.keymap(&bytes).unwrap();
        input
    }
    #[test]
    fn synthetic_launch_chord_held_through_map_never_injects_return() {
        let mut i = input();
        let held: Vec<u8> = [125u32, 28]
            .into_iter()
            .flat_map(u32::to_ne_bytes)
            .collect();
        i.enter(&held).unwrap();
        i.keys.modifiers(64, 0, 0, 0);
        i.key(28, false).unwrap();
        i.key(125, false).unwrap();
        i.keys.modifiers(0, 0, 0, 0);
        assert_eq!(i.accepted, 0);
        i.key(28, true).unwrap();
        assert_eq!(i.pending.iter().copied().collect::<Vec<_>>(), [13]);
    }
    #[test]
    fn synthetic_partial_write_then_alt_tab_finishes_exact_accepted_bytes() {
        let mut i = input();
        i.enter(&[]).unwrap();
        for key in [30, 48, 46] {
            i.key(key, true).unwrap();
            i.key(key, false).unwrap();
        }
        let mut transcript = Vec::new();
        i.write_pending(
            |bytes| {
                assert_eq!(bytes, b"abc");
                Ok(1)
            },
            &mut transcript,
        )
        .unwrap();
        i.leave();
        i.write_pending(|_| Err(io::ErrorKind::WouldBlock.into()), &mut transcript)
            .unwrap();
        i.key(30, true).unwrap(); // unfocused events cannot add text
        i.write_pending(
            |bytes| {
                assert_eq!(bytes, b"bc");
                Ok(bytes.len())
            },
            &mut transcript,
        )
        .unwrap();
        assert_eq!(transcript, b"abc");
        assert_eq!((i.accepted, i.written, i.pending_len()), (3, 3, 0));
    }
    #[test]
    fn synthetic_focus_change_held_modifier_and_old_release() {
        let mut i = input();
        i.enter(&42u32.to_ne_bytes()).unwrap();
        i.keys.modifiers(1, 0, 0, 0);
        i.key(30, true).unwrap();
        i.leave();
        i.key(30, false).unwrap();
        i.enter(&[]).unwrap();
        i.key(42, false).unwrap();
        i.keys.modifiers(0, 0, 0, 0);
        i.key(30, true).unwrap();
        assert_eq!(i.pending.iter().copied().collect::<Vec<_>>(), b"Aa");
    }
    #[test]
    fn bad_arrays_and_oversize_queue_refuse_without_erasing_accepted_input() {
        let mut i = input();
        assert!(i.enter(&[1]).is_err());
        i.enter(&[]).unwrap();
        i.key(30, true).unwrap();
        assert!(i.key(30, true).is_err());
        i.accepted = MAX_TX;
        i.key(30, false).unwrap();
        assert!(i.key(48, true).is_err());
        assert_eq!(i.pending_len(), 1);
    }
}
