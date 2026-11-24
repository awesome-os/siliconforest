//! Actual compositor-supplied XKB state; raw key numbers are never ASCII.
use xkbcommon::xkb;
pub struct Keys {
    context: xkb::Context,
    state: Option<xkb::State>,
    pub focused: bool,
}
impl Keys {
    pub fn new() -> Self {
        Self {
            context: xkb::Context::new(
                xkb::CONTEXT_NO_ENVIRONMENT_NAMES | xkb::CONTEXT_NO_DEFAULT_INCLUDES,
            ),
            state: None,
            focused: false,
        }
    }
    pub fn keymap(&mut self, bytes: &[u8]) -> Result<(), &'static str> {
        if !(2..=1024 * 1024).contains(&bytes.len())
            || bytes.last() != Some(&0)
            || bytes[..bytes.len() - 1].contains(&0)
        {
            return Err("invalid bounded XKB map");
        }
        let text =
            std::str::from_utf8(&bytes[..bytes.len() - 1]).map_err(|_| "XKB map is not UTF8")?;
        let map = xkb::Keymap::new_from_string(
            &self.context,
            text.to_owned(),
            xkb::KEYMAP_FORMAT_TEXT_V1,
            xkb::KEYMAP_COMPILE_NO_FLAGS,
        )
        .ok_or("XKB parse failed")?;
        self.state = Some(xkb::State::new(&map));
        Ok(())
    }
    pub fn ready(&self) -> bool {
        self.state.is_some() && self.focused
    }
    pub fn modifiers(&mut self, depressed: u32, latched: u32, locked: u32, group: u32) {
        if let Some(s) = self.state.as_mut() {
            s.update_mask(depressed, latched, locked, 0, 0, group);
        }
    }
    pub fn press(&self, key: u32, down: bool) -> Result<Vec<u8>, &'static str> {
        if !down || !self.focused {
            return Ok(Vec::new());
        }
        if key > 0x2ff {
            return Err("out of range Wayland key");
        }
        let state = self.state.as_ref().ok_or("key before keymap")?;
        let code = xkb::Keycode::new(key + 8);
        let sym = state.key_get_one_sym(code).raw();
        let bytes = match sym {
            0xff0d | 0xff8d => vec![13],
            0xff08 => vec![127],
            0xff09 => vec![9],
            0xff1b => vec![27],
            // Deliberately no xterm function-key/cursor-key claims for TERM=dumb.
            _ => state.key_get_utf8(code).into_bytes(),
        };
        if bytes.len() > 32 {
            return Err("oversize translated key");
        }
        Ok(bytes)
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    fn fixture() -> Keys {
        let c = xkb::Context::new(xkb::CONTEXT_NO_FLAGS);
        let m = xkb::Keymap::new_from_names(
            &c,
            "evdev",
            "pc105",
            "us",
            "",
            None,
            xkb::KEYMAP_COMPILE_NO_FLAGS,
        )
        .expect("host xkeyboard-config needed for this host-only test");
        let mut bytes = m.get_as_string(xkb::KEYMAP_FORMAT_TEXT_V1).into_bytes();
        bytes.push(0);
        let mut k = Keys::new();
        k.keymap(&bytes).unwrap();
        k.focused = true;
        k
    }
    #[test]
    fn real_xkb_case_control_return() {
        let mut k = fixture();
        assert_eq!(k.press(30, true).unwrap(), b"a");
        k.modifiers(1, 0, 0, 0);
        assert_eq!(k.press(30, true).unwrap(), b"A");
        k.modifiers(4, 0, 0, 0);
        assert_eq!(k.press(46, true).unwrap(), [3]);
        assert_eq!(k.press(28, true).unwrap(), [13]);
    }
    #[test]
    fn releases_focus_and_bad_map() {
        let mut k = fixture();
        assert!(k.press(30, false).unwrap().is_empty());
        k.focused = false;
        assert!(k.press(30, true).unwrap().is_empty());
        assert!(k.keymap(b"not xkb\0").is_err());
        assert!(k.keymap(b"bad\0middle\0").is_err());
    }
}
