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
        self.state.is_some()
    }
    pub fn modifiers(&mut self, depressed: u32, latched: u32, locked: u32, group: u32) {
        if let Some(s) = self.state.as_mut() {
            s.update_mask(depressed, latched, locked, 0, 0, group);
        }
    }
    /// What a key press means for an xterm-style terminal. `application_cursor`
    /// is the program's DECCKM setting (full-screen programs turn it on).
    pub fn press(&self, key: u32, down: bool, application_cursor: bool) -> Result<KeyAction, &'static str> {
        if !down || !self.focused {
            return Ok(KeyAction::None);
        }
        if key > 0x2ff {
            return Err("out of range Wayland key");
        }
        let state = self.state.as_ref().ok_or("key before keymap")?;
        let code = xkb::Keycode::new(key + 8);
        let sym = state.key_get_one_sym(code).raw();
        let held = |name: &str| state.mod_name_is_active(name, xkb::STATE_MODS_EFFECTIVE);
        let (shift, alt, control) = (held(xkb::MOD_NAME_SHIFT), held(xkb::MOD_NAME_ALT), held(xkb::MOD_NAME_CTRL));
        let logo = held(xkb::MOD_NAME_LOGO);
        // Copy and paste belong to the terminal: Control+Shift+C / V, as
        // Linux terminals have it; Command+C / V; and Control+Insert /
        // Shift+Insert, which is what Chonkstep's "universal copy/paste"
        // (Super+C / Super+V) sends on Linux.
        match sym {
            0x43 | 0x63 if (control && shift && !alt) || (logo && !control && !alt) => return Ok(KeyAction::Copy),
            0x56 | 0x76 if (control && shift && !alt) || (logo && !control && !alt) => return Ok(KeyAction::Paste),
            0xff63 if control && !shift && !alt => return Ok(KeyAction::Copy),
            0xff63 if shift && !control && !alt => return Ok(KeyAction::Paste),
            _ => {}
        }
        // The scrollback keys belong to the terminal, not to the program.
        match sym {
            0xff55 if shift => return Ok(KeyAction::ScrollPages(1)),
            0xff56 if shift => return Ok(KeyAction::ScrollPages(-1)),
            _ => {}
        }
        // xterm's modifier parameter: 1 + shift + 2*alt + 4*control.
        let modifier = 1 + u8::from(shift) + 2 * u8::from(alt) + 4 * u8::from(control);
        let cursor = |letter: char| {
            if modifier > 1 {
                format!("\x1b[1;{modifier}{letter}")
            } else if application_cursor {
                format!("\x1bO{letter}")
            } else {
                format!("\x1b[{letter}")
            }
        };
        let tilde = |n: u8| if modifier > 1 { format!("\x1b[{n};{modifier}~") } else { format!("\x1b[{n}~") };
        let mut bytes = match sym {
            0xff0d | 0xff8d => vec![13],
            0xff08 => vec![127],
            0xff09 => vec![9],
            0xfe20 => b"\x1b[Z".to_vec(), // Shift+Tab
            0xff1b => vec![27],
            0xff52 => cursor('A').into_bytes(),
            0xff54 => cursor('B').into_bytes(),
            0xff53 => cursor('C').into_bytes(),
            0xff51 => cursor('D').into_bytes(),
            0xff50 => cursor('H').into_bytes(),
            0xff57 => cursor('F').into_bytes(),
            0xff63 => tilde(2).into_bytes(),
            0xffff => tilde(3).into_bytes(),
            0xff55 => tilde(5).into_bytes(),
            0xff56 => tilde(6).into_bytes(),
            0xffbe..=0xffc1 => format!("\x1bO{}", (b'P' + (sym - 0xffbe) as u8) as char).into_bytes(),
            0xffc2..=0xffc9 => tilde([15, 17, 18, 19, 20, 21, 23, 24][(sym - 0xffc2) as usize]).into_bytes(),
            _ => {
                let mut text = state.key_get_utf8(code).into_bytes();
                // Alt sends the key prefixed with ESC, as xterm's metaSendsEscape.
                if alt && !text.is_empty() {
                    text.insert(0, 27);
                }
                text
            }
        };
        if bytes.len() > 32 {
            return Err("oversize translated key");
        }
        if bytes.is_empty() {
            return Ok(KeyAction::None);
        }
        Ok(KeyAction::Bytes(std::mem::take(&mut bytes)))
    }
}
/// The outcome of a key press.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum KeyAction {
    /// Nothing to do (a release, a bare modifier, no focus).
    None,
    /// Bytes for the program.
    Bytes(Vec<u8>),
    /// Move the scrollback view by this many pages (positive: back).
    ScrollPages(i32),
    /// Put the selected text on the clipboard.
    Copy,
    /// Type the clipboard's text.
    Paste,
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
        let bytes = |k: &Keys, key: u32| match k.press(key, true, false).unwrap() {
            KeyAction::Bytes(b) => b,
            other => panic!("{other:?}"),
        };
        assert_eq!(bytes(&k, 30), b"a");
        k.modifiers(1, 0, 0, 0);
        assert_eq!(bytes(&k, 30), b"A");
        k.modifiers(4, 0, 0, 0);
        assert_eq!(bytes(&k, 46), [3]);
        assert_eq!(bytes(&k, 28), [13]);
    }
    #[test]
    fn releases_focus_and_bad_map() {
        let mut k = fixture();
        assert_eq!(k.press(30, false, false).unwrap(), KeyAction::None);
        k.focused = false;
        assert_eq!(k.press(30, true, false).unwrap(), KeyAction::None);
        assert!(k.keymap(b"not xkb\0").is_err());
        assert!(k.keymap(b"bad\0middle\0").is_err());
    }
    #[test]
    fn xterm_cursor_function_and_scrollback_keys() {
        let mut k = fixture();
        let seq = |k: &Keys, key: u32, app: bool| match k.press(key, true, app).unwrap() {
            KeyAction::Bytes(b) => String::from_utf8(b).unwrap(),
            other => format!("{other:?}"),
        };
        // evdev: Up 103, Down 108, Left 105, Right 106, Home 102, End 107,
        // PageUp 104, PageDown 109, Insert 110, Delete 111, F1 59, F5 63, F12 88.
        assert_eq!((seq(&k, 103, false), seq(&k, 108, false), seq(&k, 106, false), seq(&k, 105, false)), ("\x1b[A".into(), "\x1b[B".into(), "\x1b[C".into(), "\x1b[D".into()));
        // Application cursor mode (vi, less): SS3 instead of CSI.
        assert_eq!((seq(&k, 103, true), seq(&k, 102, true), seq(&k, 107, true)), ("\x1bOA".into(), "\x1bOH".into(), "\x1bOF".into()));
        assert_eq!((seq(&k, 104, false), seq(&k, 109, false), seq(&k, 110, false), seq(&k, 111, false)), ("\x1b[5~".into(), "\x1b[6~".into(), "\x1b[2~".into(), "\x1b[3~".into()));
        assert_eq!((seq(&k, 59, false), seq(&k, 63, false), seq(&k, 88, false)), ("\x1bOP".into(), "\x1b[15~".into(), "\x1b[24~".into()));
        // Control+Right: the modifier form, in either cursor mode.
        k.modifiers(4, 0, 0, 0);
        assert_eq!((seq(&k, 106, false), seq(&k, 106, true)), ("\x1b[1;5C".into(), "\x1b[1;5C".into()));
        // Shift+PageUp/PageDown scroll the terminal's own history.
        k.modifiers(1, 0, 0, 0);
        assert_eq!(k.press(104, true, false).unwrap(), KeyAction::ScrollPages(1));
        assert_eq!(k.press(109, true, true).unwrap(), KeyAction::ScrollPages(-1));
        // Copy and paste: Control+Shift+C / V, Command+C / V, Control+Insert / Shift+Insert.
        // (evdev: c 46, v 47, Insert 110; masks: Shift 1, Control 4, Logo 64.)
        k.modifiers(5, 0, 0, 0);
        assert_eq!((k.press(46, true, false).unwrap(), k.press(47, true, false).unwrap()), (KeyAction::Copy, KeyAction::Paste));
        k.modifiers(64, 0, 0, 0);
        assert_eq!((k.press(46, true, false).unwrap(), k.press(47, true, false).unwrap()), (KeyAction::Copy, KeyAction::Paste));
        k.modifiers(4, 0, 0, 0);
        assert_eq!(k.press(110, true, false).unwrap(), KeyAction::Copy);
        // Control+C alone is still the interrupt character, Control+V the literal-next one.
        assert_eq!((seq(&k, 46, false), seq(&k, 47, false)), ("\x03".into(), "\x16".into()));
        k.modifiers(1, 0, 0, 0);
        assert_eq!(k.press(110, true, false).unwrap(), KeyAction::Paste);
        // Alt+x is ESC x; a bare modifier key is nothing.
        k.modifiers(8, 0, 0, 0);
        assert_eq!(seq(&k, 45, false), "\x1bx");
        k.modifiers(0, 0, 0, 0);
        assert_eq!(k.press(42, true, false).unwrap(), KeyAction::None);
    }
}
