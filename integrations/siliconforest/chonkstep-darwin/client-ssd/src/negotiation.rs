//! Pending decoration mode becomes effective with xdg_surface.configure.
use wayland_client::WEnum;
use wayland_protocols::xdg::decoration::zv1::client::zxdg_toplevel_decoration_v1::Mode;

#[derive(Default)]
pub struct Negotiation {
    pending: Option<Mode>,
    current: Option<Mode>,
    serial: Option<u32>,
    error: Option<&'static str>,
}

impl Negotiation {
    pub fn decoration_configured(&mut self, mode: WEnum<Mode>) {
        match mode {
            WEnum::Value(Mode::ServerSide) => self.pending = Some(Mode::ServerSide),
            WEnum::Value(Mode::ClientSide) => self.pending = Some(Mode::ClientSide),
            _ => self.error = Some("SSD required: unknown decoration configure mode"),
        }
    }

    pub fn surface_configured(&mut self, serial: u32) {
        self.serial = Some(serial);
        if let Some(mode) = self.pending.take() {
            self.current = Some(mode);
            if mode != Mode::ServerSide {
                // The compositor may legally choose CSD; this SSD experiment must refuse it.
                self.error = Some("SSD required: compositor configured client-side decorations");
            }
        }
    }

    pub fn ready(&self) -> bool {
        self.error.is_none() && self.pending.is_none()
            && self.current == Some(Mode::ServerSide) && self.serial.is_some()
    }
    pub fn serial(&self) -> Option<u32> { self.serial }
    pub fn error(&self) -> Option<&'static str> { self.error }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn surface_configure_alone_never_authorizes_attachment() {
        let mut s = Negotiation::default();
        assert!(!s.ready());
        s.surface_configured(1);
        assert!(!s.ready());
        s.decoration_configured(WEnum::Value(Mode::ServerSide));
        assert!(!s.ready()); // old ack cannot apply a new pending decoration
        s.surface_configured(2);
        assert!(s.ready());
        assert_eq!(s.serial(), Some(2));
    }

    #[test]
    fn decoration_requires_corresponding_surface_configure() {
        let mut s = Negotiation::default();
        s.decoration_configured(WEnum::Value(Mode::ServerSide));
        assert!(!s.ready());
        s.surface_configured(0); // serials are opaque, including zero/wrap
        assert!(s.ready());
        s.surface_configured(u32::MAX);
        assert!(s.ready()); // unchanged decoration state persists
    }

    #[test]
    fn newer_pending_mode_blocks_frames_and_csd_is_refused() {
        let mut s = Negotiation::default();
        s.decoration_configured(WEnum::Value(Mode::ServerSide));
        s.surface_configured(4);
        assert!(s.ready());
        s.decoration_configured(WEnum::Value(Mode::ClientSide));
        assert!(!s.ready());
        s.surface_configured(5);
        assert!(s.error().is_some());
        s.decoration_configured(WEnum::Value(Mode::ServerSide));
        s.surface_configured(6);
        assert!(!s.ready()); // failure cannot be erased by a later configure
    }

    #[test]
    fn last_pending_mode_is_applied_atomically() {
        let mut s = Negotiation::default();
        s.decoration_configured(WEnum::Value(Mode::ClientSide));
        s.decoration_configured(WEnum::Value(Mode::ServerSide));
        assert!(!s.ready());
        s.surface_configured(8);
        assert!(s.ready());
    }

    #[test]
    fn unknown_mode_is_a_sticky_failure() {
        let mut s = Negotiation::default();
        s.decoration_configured(WEnum::Unknown(99));
        s.surface_configured(1);
        assert!(!s.ready());
        assert!(s.error().is_some());
        s.decoration_configured(WEnum::Value(Mode::ServerSide));
        s.surface_configured(2);
        assert!(!s.ready());
    }
}
