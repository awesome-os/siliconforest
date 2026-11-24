//! A narrow observer of real Wayland input. This module never injects events.
pub const KEYMAP_LIMIT: usize = 1 << 20;
pub const INPUT_DEADLINE_MS: u64 = 15_000;
pub const CONTENT_POINT: (f64, f64) = (200.0, 100.0);

pub fn keymap_fingerprint(bytes: &[u8]) -> Result<u64, &'static str> {
    if bytes.len() < 2 || bytes.len() > KEYMAP_LIMIT || bytes.last() != Some(&0) {
        return Err("invalid XKB keymap size or terminator");
    }
    let text = std::str::from_utf8(&bytes[..bytes.len() - 1]).map_err(|_| "keymap is not UTF-8")?;
    if text.contains('\0') || !text.trim_start().starts_with("xkb_keymap")
        || !["xkb_keycodes", "xkb_types", "xkb_compatibility", "xkb_symbols"]
            .iter().all(|section| text.contains(section)) {
        return Err("not a complete XKB text keymap shape");
    }
    // A diagnostic byte identity, NOT a cryptographic identity or semantic XKB parse.
    Ok(bytes.iter().fold(0xcbf29ce484222325u64, |h, b| (h ^ u64::from(*b)).wrapping_mul(0x100000001b3)))
}

#[derive(Default)]
pub struct InputProof {
    seat: bool, keymap: bool, focus: bool, neutral: bool, ssd: bool, frame: bool,
    armed_at: Option<u64>, keys: u8, buttons: u8, pointer_focus: bool,
    pointer_seen: bool, position: Option<(f64, f64)>, release_frame: bool,
    failure: Option<&'static str>,
}
impl InputProof {
    pub fn reject(&mut self, why: &'static str) { self.failure.get_or_insert(why); }
    pub fn error(&self) -> Option<&'static str> { self.failure }
    pub fn seat(&mut self, exact_keyboard_pointer: bool) {
        if !exact_keyboard_pointer { self.reject("seat capabilities differ from keyboard+pointer"); }
        else { self.seat = true; }
    }
    pub fn keymap(&mut self) { self.keymap = true; }
    pub fn keyboard_enter(&mut self, own: bool, empty_keys: bool) {
        if !own || !empty_keys { self.reject("keyboard focus is wrong or keys are already held"); }
        else { self.focus = true; }
    }
    pub fn keyboard_leave(&mut self) { self.focus = false; self.reject("keyboard focus lost during input experiment"); }
    pub fn modifiers(&mut self, depressed: u32, latched: u32, locked: u32, group: u32) {
        if depressed | latched | locked | group != 0 { self.reject("non-neutral modifiers/group"); }
        else { self.neutral = true; }
    }
    pub fn ssd(&mut self) { self.ssd = true; }
    pub fn frame(&mut self) { self.frame = true; }
    pub fn ready(&self) -> bool {
        self.failure.is_none() && self.seat && self.keymap && self.focus && self.neutral && self.ssd && self.frame
    }
    pub fn arm(&mut self, now: u64) -> bool {
        if !self.ready() || self.armed_at.is_some() { return false; }
        self.armed_at = Some(now); true
    }
    pub fn tick(&mut self, now: u64) {
        if self.armed_at.is_some_and(|start| now.saturating_sub(start) >= INPUT_DEADLINE_MS) && !self.complete() {
            self.reject("host-origin input observation deadline expired");
        }
    }
    pub fn key(&mut self, evdev: u32, down: bool) {
        if self.armed_at.is_none() || !self.focus || !self.neutral || evdev != 30
            || !matches!((self.keys, down), (0, true) | (1, false)) {
            self.reject("unexpected key, state, order or focus (expected A press/release only)");
        } else { self.keys += 1; }
    }
    fn point_ok(x: f64, y: f64) -> bool {
        x.is_finite() && y.is_finite() && (x - CONTENT_POINT.0).abs() <= 1.0 && (y - CONTENT_POINT.1).abs() <= 1.0
    }
    pub fn pointer_enter(&mut self, own: bool, x: f64, y: f64) {
        if self.armed_at.is_none() || self.keys != 2 || !own || !Self::point_ok(x, y) {
            self.reject("pointer enter differs from expected own content point after A release");
        } else { self.pointer_focus = true; self.pointer_seen = true; self.position = Some((x, y)); }
    }
    pub fn motion(&mut self, x: f64, y: f64) {
        if !self.pointer_focus || !x.is_finite() || !y.is_finite()
            || (self.buttons < 2 && !Self::point_ok(x, y)) {
            self.reject("pointer motion before focus or outside expected click point");
        } else { self.position = Some((x, y)); }
    }
    pub fn pointer_leave(&mut self, own: bool) {
        if !own || self.buttons != 2 { self.reject("pointer left before complete content click"); }
        self.pointer_focus = false;
    }
    pub fn button(&mut self, button: u32, down: bool) {
        if self.armed_at.is_none() || self.keys != 2 || !self.pointer_focus
            || !self.position.is_some_and(|(x, y)| Self::point_ok(x, y)) || button != 0x110
            || !matches!((self.buttons, down), (0, true) | (1, false)) {
            self.reject("unexpected button, coordinates or order (expected one left click)");
        } else { self.buttons += 1; }
    }
    pub fn pointer_frame(&mut self) { if self.buttons == 2 { self.release_frame = true; } }
    pub fn pointer_position(&self) -> Option<(f64, f64)> { self.position }
    pub fn complete(&self) -> bool {
        self.failure.is_none() && self.ready() && self.armed_at.is_some() && self.keys == 2
            && self.pointer_seen && self.buttons == 2 && self.release_frame
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn ready() -> InputProof {
        let mut p=InputProof::default(); p.seat(true); p.keymap(); p.keyboard_enter(true,true);
        p.modifiers(0,0,0,0); p.ssd(); p.frame(); assert!(p.arm(100)); p
    }
    fn keys(p: &mut InputProof) { p.key(30,true); p.key(30,false); }
    fn click(p: &mut InputProof) { p.pointer_enter(true,200.,100.); p.button(0x110,true); p.button(0x110,false); }
    #[test] fn readiness_needs_every_actual_prerequisite() {
        let mut p=InputProof::default(); assert!(!p.arm(0)); p.seat(true); p.keymap();
        p.keyboard_enter(true,true); p.modifiers(0,0,0,0); p.ssd(); assert!(!p.arm(0));
        p.frame(); assert!(p.arm(0)); assert!(!p.arm(1)); assert!(!p.complete());
    }
    #[test] fn complete_requires_both_pairs_and_pointer_frame() {
        let mut p=ready(); keys(&mut p); assert!(!p.complete()); click(&mut p);
        assert!(!p.complete()); p.pointer_frame(); assert!(p.complete());
        p.pointer_leave(true); assert!(p.complete()); // server-owned titlebar drag is not a client click
    }
    #[test] fn bad_capability_focus_or_modifiers_is_sticky() {
        for bad in 0..4 { let mut p=ready(); match bad {0=>p.seat(false),1=>p.keyboard_enter(false,true),
            2=>p.keyboard_enter(true,false),_=>p.modifiers(1,0,0,0)}
            p.seat(true); p.keyboard_enter(true,true); p.modifiers(0,0,0,0);
            keys(&mut p); click(&mut p); p.pointer_frame(); assert!(!p.complete()); assert!(p.error().is_some()); }
    }
    #[test] fn key_wrong_release_repeat_or_late_extra_fails() {
        for events in [vec![(31,true)],vec![(30,false)],vec![(30,true),(30,true)],vec![(30,true),(30,false),(30,true)]] {
            let mut p=ready(); for (key,down) in events {p.key(key,down);} assert!(p.error().is_some()); }
        let mut p=InputProof::default(); p.key(30,true); assert!(p.error().is_some());
    }
    #[test] fn pointer_must_enter_own_surface_after_keys_at_expected_point() {
        for (own,x,y) in [(false,200.,100.),(true,202.,100.),(true,f64::NAN,100.),(true,200.,f64::INFINITY)] {
            let mut p=ready(); keys(&mut p); p.pointer_enter(own,x,y); assert!(p.error().is_some()); }
        let mut p=ready(); p.pointer_enter(true,200.,100.); assert!(p.error().is_some());
    }
    #[test] fn motion_buttons_and_leave_do_not_forge_click() {
        let mut p=ready(); keys(&mut p); p.motion(200.,100.); assert!(p.error().is_some());
        for which in 0..4 { let mut p=ready(); keys(&mut p); p.pointer_enter(true,200.,100.);
            match which {0=>p.button(0x111,true),1=>p.button(0x110,false),2=>p.pointer_leave(true),_=>p.motion(210.,100.)}
            assert!(p.error().is_some()); }
    }
    #[test] fn tolerance_is_bounded_and_no_second_click_is_allowed() {
        let mut p=ready(); keys(&mut p); p.pointer_enter(true,199.,101.);
        p.button(0x110,true); p.button(0x110,false); p.pointer_frame(); assert!(p.complete());
        p.button(0x110,true); assert!(!p.complete());
    }
    #[test] fn focus_loss_and_deadlines_remain_failure() {
        let mut p=ready(); keys(&mut p); click(&mut p); p.pointer_frame(); p.keyboard_leave(); assert!(!p.complete());
        let mut p=ready(); p.tick(15_099); assert!(p.error().is_none()); p.tick(15_100); assert!(p.error().is_some());
        keys(&mut p); click(&mut p); p.pointer_frame(); assert!(!p.complete());
    }
    #[test] fn keymap_shape_is_bounded_and_identity_changes_with_bytes() {
        let good=b"xkb_keymap { xkb_keycodes {}; xkb_types {}; xkb_compatibility {}; xkb_symbols {}; };\0";
        assert!(keymap_fingerprint(good).is_ok()); let mut changed=good.to_vec(); changed.insert(0,b' ');
        assert_ne!(keymap_fingerprint(good),keymap_fingerprint(&changed));
        for bad in [b"".as_slice(),b"xkb_keymap\0",&good[..good.len()-1],b"xkb_keymap\0junk\0"] {assert!(keymap_fingerprint(bad).is_err());}
        assert!(keymap_fingerprint(&vec![b' ';KEYMAP_LIMIT+1]).is_err());
    }
}
