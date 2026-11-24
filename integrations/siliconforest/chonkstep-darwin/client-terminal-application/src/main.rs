//! Owned Darwin desktop terminal application; real /bin/sh PTY and Wayland window.
mod term;
mod clipboard;
#[cfg(test)]
mod host_wayland_tests;
mod input;
mod keys;
mod negotiation;
mod protocol;
mod pty;
mod render;
mod scale;
mod shm;
mod status;
use protocol::{FrameState, MAX_RX};
use std::{
    fs::{File, OpenOptions},
    io::{self, Read, Seek, SeekFrom, Write},
    os::{
        fd::{AsFd, AsRawFd, FromRawFd, OwnedFd},
        unix::{
            ffi::OsStrExt,
            fs::{MetadataExt, OpenOptionsExt, PermissionsExt},
            net::UnixStream,
        },
    },
    path::{Path, PathBuf},
    sync::atomic::{AtomicBool, Ordering},
    time::{Duration, Instant},
};
use wayland_client::{
    delegate_noop,
    protocol::{
        wl_buffer, wl_callback, wl_compositor, wl_data_device, wl_data_device_manager,
        wl_data_offer, wl_data_source, wl_keyboard, wl_output, wl_pointer, wl_registry, wl_seat, wl_shm,
        wl_shm_pool, wl_surface,
    },
    Connection, Dispatch, QueueHandle, WEnum,
};
use wayland_protocols::wp::{
    fractional_scale::v1::client::{wp_fractional_scale_manager_v1, wp_fractional_scale_v1},
    viewporter::client::{wp_viewport, wp_viewporter},
};
use wayland_protocols::xdg::{
    decoration::zv1::client::{zxdg_decoration_manager_v1, zxdg_toplevel_decoration_v1},
    shell::client::{xdg_surface, xdg_toplevel, xdg_wm_base},
};
type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;
static STOP: AtomicBool = AtomicBool::new(false);
extern "C" fn stop(_: i32) {
    STOP.store(true, Ordering::Relaxed);
}
fn signals() -> io::Result<()> {
    unsafe {
        for sig in [libc::SIGTERM, libc::SIGINT, libc::SIGHUP] {
            let mut a: libc::sigaction = std::mem::zeroed();
            a.sa_sigaction = stop as *const () as usize;
            libc::sigemptyset(&mut a.sa_mask);
            if libc::sigaction(sig, &a, std::ptr::null_mut()) < 0 {
                return Err(io::Error::last_os_error());
            }
        }
        for (sig, action) in [
            (libc::SIGPIPE, libc::SIG_IGN),
            (libc::SIGCHLD, libc::SIG_DFL),
        ] {
            if libc::signal(sig, action) == libc::SIG_ERR {
                return Err(io::Error::last_os_error());
            }
        }
    }
    Ok(())
}
struct State {
    compositor: Option<wl_compositor::WlCompositor>,
    shm: Option<wl_shm::WlShm>,
    shell: Option<xdg_wm_base::XdgWmBase>,
    decoration: Option<zxdg_decoration_manager_v1::ZxdgDecorationManagerV1>,
    seat: Option<wl_seat::WlSeat>,
    keyboard: Option<wl_keyboard::WlKeyboard>,
    pointer: Option<wl_pointer::WlPointer>,
    /// Scrollback requests not yet applied: pages from Shift+PageUp/PageDown,
    /// and wheel travel in Wayland axis units (10 per detent; negative: up).
    scroll_pages: i32,
    wheel: f64,
    /// Bytes were typed since the last look: the view returns to the live screen.
    typed: bool,
    /// The clipboard: the manager, this seat's device, the offers announced
    /// (with their types), which of them is the current selection, and our
    /// own source with the text it offers.
    data_manager: Option<wl_data_device_manager::WlDataDeviceManager>,
    data_device: Option<wl_data_device::WlDataDevice>,
    offers: Vec<(wl_data_offer::WlDataOffer, Vec<String>)>,
    clipboard: Option<wl_data_offer::WlDataOffer>,
    source: Option<wl_data_source::WlDataSource>,
    clip_text: String,
    outgoing: Vec<clipboard::Outgoing>,
    /// Serial of the latest key or button press (what `set_selection` wants).
    last_serial: u32,
    /// Selecting with the pointer: where it is (view cell), where the drag
    /// began, the highlight to apply, and whether a drag just ended.
    pointer_cell: (usize, usize),
    drag: Option<(usize, usize)>,
    select: Option<Option<((usize, usize), (usize, usize))>>,
    select_done: bool,
    own: Option<wl_surface::WlSurface>,
    registry_done: bool,
    closed: bool,
    error: Option<String>,
    ssd: negotiation::Negotiation,
    input: input::Input,
    instance: String,
    ready: bool,
    frames: FrameState,
    /// Output scale. Optional globals, and what each source last said: the
    /// fractional protocol (120ths), `wl_surface.preferred_buffer_scale`, the
    /// outputs (registry name, object, scale) and those the surface is on.
    fractional_manager: Option<wp_fractional_scale_manager_v1::WpFractionalScaleManagerV1>,
    viewporter: Option<wp_viewporter::WpViewporter>,
    /// The window has a viewport and a fractional-scale object: fractional
    /// scales can be presented.
    fractional_mode: bool,
    fractional: Option<u32>,
    preferred: Option<i32>,
    outputs: Vec<(u32, wl_output::WlOutput, i32)>,
    entered: Vec<wl_output::WlOutput>,
    /// Font size in logical pixels, and the cell metrics of the buffer on
    /// screen (pointer positions are mapped with these).
    base_px: u32,
    metrics: scale::Metrics,
    /// Logical sizes: asked for by a toplevel configure not yet acknowledged,
    /// the last one acknowledged (`None`: the compositor left it to us), and
    /// what is committed.
    pending_size: Option<(usize, usize)>,
    /// A configure was acknowledged and no commit has carried the
    /// acknowledgement to the compositor yet.
    ack_uncommitted: bool,
    configured: Option<(usize, usize)>,
    logical: (usize, usize),
    dirty: bool,
}
impl State {
    fn new(instance: String) -> Self {
        let base_px = scale::font_px(std::env::var(scale::FONT_ENV).ok().as_deref());
        let metrics = scale::Metrics::new(base_px, scale::UNIT);
        Self {
            compositor: None,
            shm: None,
            shell: None,
            decoration: None,
            seat: None,
            keyboard: None,
            pointer: None,
            scroll_pages: 0,
            wheel: 0.0,
            typed: false,
            data_manager: None,
            data_device: None,
            offers: Vec::new(),
            clipboard: None,
            source: None,
            clip_text: String::new(),
            outgoing: Vec::new(),
            last_serial: 0,
            pointer_cell: (0, 0),
            drag: None,
            select: None,
            select_done: false,
            own: None,
            registry_done: false,
            closed: false,
            error: None,
            ssd: Default::default(),
            input: input::Input::new(),
            instance,
            ready: false,
            frames: FrameState::new(),
            fractional_manager: None,
            viewporter: None,
            fractional_mode: false,
            fractional: None,
            preferred: None,
            outputs: Vec::new(),
            entered: Vec::new(),
            base_px,
            metrics,
            pending_size: None,
            ack_uncommitted: false,
            configured: None,
            logical: scale::layout(&metrics, None, (80, 24), false).logical,
            dirty: true,
        }
    }
    /// The effective scale in 120ths (see `scale::effective`). Of several
    /// outputs the surface is on, the largest scale counts.
    fn scale120(&self) -> u32 {
        let output = self.outputs.iter().filter(|(_, o, _)| self.entered.contains(o)).map(|(_, _, scale)| *scale).max();
        scale::effective(self.fractional.filter(|_| self.fractional_mode), self.preferred, output)
    }
    fn reject(&mut self, e: impl ToString) {
        if self.error.is_none() {
            self.error = Some(e.to_string());
        }
    }
}
impl Dispatch<wl_registry::WlRegistry, ()> for State {
    fn event(
        s: &mut Self,
        r: &wl_registry::WlRegistry,
        e: wl_registry::Event,
        _: &(),
        _: &Connection,
        q: &QueueHandle<Self>,
    ) {
        if let wl_registry::Event::GlobalRemove { name } = e {
            if let Some(at) = s.outputs.iter().position(|(n, _, _)| *n == name) {
                let (_, output, _) = s.outputs.remove(at);
                s.entered.retain(|o| *o != output);
                if wayland_client::Proxy::version(&output) >= 3 {
                    output.release();
                }
                s.dirty = true;
            }
        } else if let wl_registry::Event::Global {
            name,
            interface,
            version,
        } = e
        {
            match interface.as_str() {
                "wl_compositor" => {
                    if s.compositor.is_some() || version < 4 {
                        s.reject("ambiguous/old compositor")
                    } else {
                        // Version 6 has wl_surface.preferred_buffer_scale.
                        s.compositor = Some(r.bind(name, version.min(6), q, ()));
                    }
                }
                // Optional, for the output scale: all absent means scale 1.
                "wl_output" if s.outputs.len() < 16 => {
                    s.outputs.push((name, r.bind(name, version.min(4), q, ()), 1));
                }
                "wp_fractional_scale_manager_v1" if s.fractional_manager.is_none() => {
                    s.fractional_manager = Some(r.bind(name, 1, q, ()));
                }
                "wp_viewporter" if s.viewporter.is_none() => {
                    s.viewporter = Some(r.bind(name, 1, q, ()));
                }
                "wl_shm" => s.shm = Some(r.bind(name, 1, q, ())),
                "xdg_wm_base" => s.shell = Some(r.bind(name, version.min(6), q, ())),
                "zxdg_decoration_manager_v1" => s.decoration = Some(r.bind(name, 1, q, ())),
                // Optional: without it there is selection but no copy or paste.
                "wl_data_device_manager" if s.data_manager.is_none() => {
                    s.data_manager = Some(r.bind(name, version.min(3), q, ()));
                }
                "wl_seat" => {
                    if s.seat.is_some() || version < 5 {
                        s.reject("ambiguous/old seat")
                    } else {
                        s.seat = Some(r.bind(name, version.min(9), q, ()));
                    }
                }
                _ => (),
            }
        }
    }
}
impl Dispatch<wl_seat::WlSeat, ()> for State {
    fn event(
        s: &mut Self,
        p: &wl_seat::WlSeat,
        e: wl_seat::Event,
        _: &(),
        _: &Connection,
        q: &QueueHandle<Self>,
    ) {
        if let wl_seat::Event::Capabilities { capabilities } = e {
            if matches!(capabilities,WEnum::Value(c) if c.contains(wl_seat::Capability::Keyboard)) {
                if s.keyboard.is_none() {
                    s.keyboard = Some(p.get_keyboard(q, ()));
                }
            } else if s.keyboard.is_some() {
                s.reject("keyboard capability removed");
            }
            // The pointer: the wheel (scrollback) and selecting text.
            if matches!(capabilities,WEnum::Value(c) if c.contains(wl_seat::Capability::Pointer)) {
                if s.pointer.is_none() {
                    s.pointer = Some(p.get_pointer(q, ()));
                }
            } else if let Some(pointer) = s.pointer.take() {
                pointer.release(); // the seat is bound at version 5 or later
            }
        }
    }
}
impl Dispatch<wl_pointer::WlPointer, ()> for State {
    fn event(
        s: &mut Self,
        _: &wl_pointer::WlPointer,
        e: wl_pointer::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
        // Positions are in logical surface units; cells are device pixels.
        let metrics = s.metrics;
        let cell = |x: f64, y: f64| metrics.cell_at(x, y);
        match e {
            wl_pointer::Event::Axis { axis: WEnum::Value(wl_pointer::Axis::VerticalScroll), value, .. } => {
                // Axis values are logical too: the same travel is the same number
                // of lines at every scale.
                // Bounded: a flood of axis events cannot build an unbounded backlog.
                s.wheel = (s.wheel + value).clamp(-100_000.0, 100_000.0);
            }
            wl_pointer::Event::Enter { surface_x, surface_y, .. } => s.pointer_cell = cell(surface_x, surface_y),
            wl_pointer::Event::Motion { surface_x, surface_y, .. } => {
                s.pointer_cell = cell(surface_x, surface_y);
                // Dragging with the button held: the highlight follows.
                if let Some(anchor) = s.drag {
                    s.select = Some(Some((anchor, s.pointer_cell)));
                }
            }
            wl_pointer::Event::Button { serial, button: 0x110, state, .. } => match state {
                WEnum::Value(wl_pointer::ButtonState::Pressed) => {
                    s.last_serial = serial;
                    // A press alone takes the highlight away; a drag makes a new one.
                    s.drag = Some(s.pointer_cell);
                    s.select = Some(None);
                }
                _ => {
                    if let Some(anchor) = s.drag.take() {
                        if anchor != s.pointer_cell {
                            s.select = Some(Some((anchor, s.pointer_cell)));
                            s.select_done = true;
                        }
                    }
                }
            },
            wl_pointer::Event::Leave { .. } => {
                // The release may never arrive here: keep what is highlighted.
                if s.drag.take().is_some() {
                    s.select_done = true;
                }
            }
            _ => {}
        }
    }
}
impl Dispatch<wl_keyboard::WlKeyboard, ()> for State {
    fn event(
        s: &mut Self,
        _: &wl_keyboard::WlKeyboard,
        e: wl_keyboard::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
        match e {
            wl_keyboard::Event::Keymap { format, fd, size } => {
                let result = (|| -> Result<()> {
                    if !matches!(format, WEnum::Value(wl_keyboard::KeymapFormat::XkbV1))
                        || !(2..=1048576).contains(&size)
                    {
                        return Err("invalid keymap format/size".into());
                    }
                    let mut f = File::from(fd);
                    let m = f.metadata()?;
                    if !m.is_file() || m.len() < u64::from(size) {
                        return Err("invalid keymap fd".into());
                    }
                    f.seek(SeekFrom::Start(0))?;
                    let mut b = vec![0; size as usize];
                    f.read_exact(&mut b)?;
                    s.input.keys.keymap(&b)?;
                    Ok(())
                })();
                if let Err(e) = result {
                    s.reject(e);
                }
            }
            wl_keyboard::Event::Enter { surface, keys, .. } => {
                if s.own.as_ref() != Some(&surface) {
                    s.reject("focus entered foreign surface");
                    return;
                }
                if let Err(e) = s.input.enter(&keys) {
                    s.reject(e);
                }
            }
            wl_keyboard::Event::Leave { surface, .. } => {
                if s.own.as_ref() != Some(&surface) {
                    s.reject("focus left foreign surface");
                    return;
                }
                s.input.leave();
            }
            wl_keyboard::Event::Modifiers {
                mods_depressed,
                mods_latched,
                mods_locked,
                group,
                ..
            } => {
                s.input
                    .keys
                    .modifiers(mods_depressed, mods_latched, mods_locked, group);
            }
            wl_keyboard::Event::Key { key, state, serial, .. } => {
                let down = match state {
                    WEnum::Value(wl_keyboard::KeyState::Pressed) => {
                        s.last_serial = serial;
                        true
                    }
                    WEnum::Value(wl_keyboard::KeyState::Released) => false,
                    _ => {
                        s.reject("invalid key state");
                        return;
                    }
                };
                match s.input.key(key, down) {
                    Ok((pages, typed)) => {
                        s.scroll_pages = (s.scroll_pages + pages.unwrap_or(0)).clamp(-1000, 1000);
                        s.typed |= typed;
                    }
                    Err(e) => s.reject(e),
                }
            }
            _ => (),
        }
    }
}
delegate_noop!(State: ignore wl_data_device_manager::WlDataDeviceManager);
impl Dispatch<wl_data_device::WlDataDevice, ()> for State {
    fn event(
        s: &mut Self,
        _: &wl_data_device::WlDataDevice,
        e: wl_data_device::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
        match e {
            wl_data_device::Event::DataOffer { id } => {
                // Bounded: stale offers are destroyed when a selection arrives.
                if s.offers.len() >= 16 {
                    let (old, _) = s.offers.remove(0);
                    if s.clipboard.as_ref() != Some(&old) {
                        old.destroy();
                    }
                }
                s.offers.push((id, Vec::new()));
            }
            wl_data_device::Event::Selection { id } => {
                // Every other offer is finished with: this one (or none) is the clipboard.
                for (offer, _) in &s.offers {
                    if Some(offer) != id.as_ref() {
                        offer.destroy();
                    }
                }
                s.offers.retain(|(offer, _)| Some(offer) == id.as_ref());
                s.clipboard = id;
            }
            _ => {}
        }
    }
    wayland_client::event_created_child!(State, wl_data_device::WlDataDevice, [
        wl_data_device::EVT_DATA_OFFER_OPCODE => (wl_data_offer::WlDataOffer, ()),
    ]);
}
impl Dispatch<wl_data_offer::WlDataOffer, ()> for State {
    fn event(
        s: &mut Self,
        offer: &wl_data_offer::WlDataOffer,
        e: wl_data_offer::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
        if let wl_data_offer::Event::Offer { mime_type } = e {
            if let Some((_, types)) = s.offers.iter_mut().find(|(o, _)| o == offer) {
                if types.len() < 64 && mime_type.len() <= 128 {
                    types.push(mime_type);
                }
            }
        }
    }
}
impl Dispatch<wl_data_source::WlDataSource, ()> for State {
    fn event(
        s: &mut Self,
        source: &wl_data_source::WlDataSource,
        e: wl_data_source::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
        match e {
            // Someone pastes what was copied here.
            wl_data_source::Event::Send { mime_type, fd } => {
                if s.source.as_ref() == Some(source) && clipboard::MIMES.contains(&mime_type.as_str()) && s.outgoing.len() < 8 {
                    if let Ok(out) = clipboard::Outgoing::new(fd, s.clip_text.clone().into_bytes()) {
                        s.outgoing.push(out);
                    }
                }
            }
            // Another client copied something: this source is over.
            wl_data_source::Event::Cancelled => {
                source.destroy();
                if s.source.as_ref() == Some(source) {
                    s.source = None;
                }
            }
            _ => {}
        }
    }
}
impl Dispatch<xdg_wm_base::XdgWmBase, ()> for State {
    fn event(
        _: &mut Self,
        p: &xdg_wm_base::XdgWmBase,
        e: xdg_wm_base::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
        if let xdg_wm_base::Event::Ping { serial } = e {
            p.pong(serial);
        }
    }
}
impl Dispatch<xdg_surface::XdgSurface, ()> for State {
    fn event(
        s: &mut Self,
        p: &xdg_surface::XdgSurface,
        e: xdg_surface::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
        if let xdg_surface::Event::Configure { serial } = e {
            p.ack_configure(serial);
            // An acknowledgement takes effect with the next commit.
            s.ack_uncommitted = true;
            s.dirty = true;
            s.ssd.surface_configured(serial);
            if let Some(size) = s.pending_size.take() {
                s.configured = Some(size);
                s.dirty = true;
            }
        }
    }
}
impl Dispatch<zxdg_toplevel_decoration_v1::ZxdgToplevelDecorationV1, ()> for State {
    fn event(
        s: &mut Self,
        _: &zxdg_toplevel_decoration_v1::ZxdgToplevelDecorationV1,
        e: zxdg_toplevel_decoration_v1::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
        if let zxdg_toplevel_decoration_v1::Event::Configure { mode } = e {
            s.ssd.decoration_configured(mode);
        }
    }
}
impl Dispatch<xdg_toplevel::XdgToplevel, ()> for State {
    fn event(
        s: &mut Self,
        _: &xdg_toplevel::XdgToplevel,
        e: xdg_toplevel::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
        match e {
            xdg_toplevel::Event::Close => s.closed = true,
            xdg_toplevel::Event::Configure { width, height, .. } => {
                if width == 0 && height == 0 {
                    return;
                }
                // Logical units, like everything in xdg-shell.
                let w = if width == 0 { s.logical.0 } else { width.max(0) as usize };
                let h = if height == 0 {
                    s.logical.1
                } else {
                    height.max(0) as usize
                };
                // Strict against a compositor that does not scale, as before.
                let scales = s.fractional_mode || s.preferred.is_some() || !s.outputs.is_empty();
                let (low, high) = scale::Metrics::new(s.base_px, s.scale120()).configure_bounds(scales);
                if !(low.0..=high.0).contains(&w) || !(low.1..=high.1).contains(&h) {
                    s.reject("configure dimensions outside bounded grid");
                } else {
                    s.pending_size = Some((w, h));
                }
            }
            _ => (),
        }
    }
}
impl Dispatch<wl_buffer::WlBuffer, ()> for State {
    fn event(
        s: &mut Self,
        _: &wl_buffer::WlBuffer,
        e: wl_buffer::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
        if let wl_buffer::Event::Release = e {
            s.frames.release();
        }
    }
}
impl Dispatch<wl_callback::WlCallback, u8> for State {
    fn event(
        s: &mut Self,
        _: &wl_callback::WlCallback,
        e: wl_callback::Event,
        kind: &u8,
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
        if let wl_callback::Event::Done { .. } = e {
            if *kind == 0 {
                s.registry_done = true;
            } else {
                s.frames.done();
            }
        }
    }
}
impl Dispatch<wl_surface::WlSurface, ()> for State {
    fn event(
        s: &mut Self,
        _: &wl_surface::WlSurface,
        e: wl_surface::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
        match e {
            wl_surface::Event::Enter { output } => {
                if !s.entered.contains(&output) && s.entered.len() < 16 {
                    s.entered.push(output);
                }
            }
            wl_surface::Event::Leave { output } => s.entered.retain(|o| *o != output),
            wl_surface::Event::PreferredBufferScale { factor } => s.preferred = Some(factor),
            _ => return,
        }
        // The session loop compares the effective scale with what is shown.
        s.dirty = true;
    }
}
impl Dispatch<wl_output::WlOutput, ()> for State {
    fn event(
        s: &mut Self,
        output: &wl_output::WlOutput,
        e: wl_output::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
        if let wl_output::Event::Scale { factor } = e {
            if let Some(known) = s.outputs.iter_mut().find(|(_, o, _)| o == output) {
                known.2 = factor;
                s.dirty = true;
            }
        }
    }
}
impl Dispatch<wp_fractional_scale_v1::WpFractionalScaleV1, ()> for State {
    fn event(
        s: &mut Self,
        _: &wp_fractional_scale_v1::WpFractionalScaleV1,
        e: wp_fractional_scale_v1::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
        if let wp_fractional_scale_v1::Event::PreferredScale { scale } = e {
            s.fractional = Some(scale);
            s.dirty = true;
        }
    }
}
delegate_noop!(State: ignore wl_compositor::WlCompositor);
delegate_noop!(State: ignore wp_fractional_scale_manager_v1::WpFractionalScaleManagerV1);
delegate_noop!(State: ignore wp_viewporter::WpViewporter);
delegate_noop!(State: ignore wp_viewport::WpViewport);
delegate_noop!(State: ignore wl_shm::WlShm);
delegate_noop!(State: ignore wl_shm_pool::WlShmPool);
delegate_noop!(State: ignore zxdg_decoration_manager_v1::ZxdgDecorationManagerV1);

struct Buffer {
    /// The pool's memory: POSIX shared memory, or the unlinked file (`src/shm.rs`).
    /// `drop` destroys the wl_buffer and the pool; this field is released
    /// after that (unmap, close).
    backing: shm::Backing,
    pool: wl_shm_pool::WlShmPool,
    buffer: wl_buffer::WlBuffer,
    size: (usize, usize),
}
impl Buffer {
    fn new(
        shm: &wl_shm::WlShm,
        q: &QueueHandle<State>,
        dir: &Path,
        size: (usize, usize),
        seq: u32,
        scale120: u32,
        policy: shm::Policy,
    ) -> Result<Self> {
        let bytes = size
            .0
            .checked_mul(size.1)
            .and_then(|n| n.checked_mul(4))
            .ok_or("buffer overflow")?;
        // 64 MiB of pixels at scale 1: the same window area at every scale.
        let unit = scale::UNIT as usize;
        if bytes > 64 * 1024 * 1024 * (scale120 as usize).div_ceil(unit).pow(2) {
            return Err("buffer exceeds bound".into());
        }
        let backing = shm::Backing::new(dir, seq, size, bytes, policy)?;
        let pool = shm.create_pool(backing.fd(), bytes as i32, q, ());
        let buffer = pool.create_buffer(
            0,
            size.0 as i32,
            size.1 as i32,
            (size.0 * 4) as i32,
            wl_shm::Format::Xrgb8888,
            q,
            (),
        );
        Ok(Self {
            backing,
            pool,
            buffer,
            size,
        })
    }
}
impl Drop for Buffer {
    fn drop(&mut self) {
        self.buffer.destroy();
        self.pool.destroy();
    }
}
struct Window {
    surface: wl_surface::WlSurface,
    xdg: xdg_surface::XdgSurface,
    top: xdg_toplevel::XdgToplevel,
    dec: zxdg_toplevel_decoration_v1::ZxdgToplevelDecorationV1,
    /// Both or neither: the fractional scale is only usable with a viewport.
    viewport: Option<wp_viewport::WpViewport>,
    fractional: Option<wp_fractional_scale_v1::WpFractionalScaleV1>,
    /// `wl_surface.set_buffer_scale` as last sent (1 until then).
    buffer_scale: i32,
}
impl Window {
    fn new(s: &mut State, q: &QueueHandle<State>) -> Result<Self> {
        if s.seat.is_none() {
            return Err("no advertised keyboard seat".into());
        }
        let surface = s
            .compositor
            .as_ref()
            .ok_or("no compositor")?
            .create_surface(q, ());
        s.own = Some(surface.clone());
        let xdg = s
            .shell
            .as_ref()
            .ok_or("no shell")?
            .get_xdg_surface(&surface, q, ());
        let top = xdg.get_toplevel(q, ());
        top.set_title(format!("Terminal {}", &s.instance[..12]));
        top.set_app_id("chonk-terminal-darwin".into());
        Self::limits(&top, &s.metrics);
        let (viewport, fractional) = match (&s.viewporter, &s.fractional_manager) {
            (Some(viewporter), Some(manager)) => (
                Some(viewporter.get_viewport(&surface, q, ())),
                Some(manager.get_fractional_scale(&surface, q, ())),
            ),
            _ => (None, None),
        };
        s.fractional_mode = viewport.is_some();
        let dec = s
            .decoration
            .as_ref()
            .ok_or("SSD manager missing")?
            .get_toplevel_decoration(&top, q, ());
        dec.set_mode(zxdg_toplevel_decoration_v1::Mode::ServerSide);
        surface.commit();
        Ok(Self {
            surface,
            xdg,
            top,
            dec,
            viewport,
            fractional,
            buffer_scale: 1,
        })
    }
    /// Smallest and largest window (20 x 6 and 160 x 60 cells), in logical units.
    fn limits(top: &xdg_toplevel::XdgToplevel, m: &scale::Metrics) {
        let logical = |cols, rows| {
            let grid = m.grid_px(cols, rows);
            (scale::to_logical(grid.0, m.scale120) as i32, scale::to_logical(grid.1, m.scale120) as i32)
        };
        let (min, max) = (logical(20, 6), logical(160, 60));
        top.set_min_size(min.0, min.1);
        top.set_max_size(max.0, max.1);
    }
}
impl Drop for Window {
    fn drop(&mut self) {
        if let Some(fractional) = self.fractional.take() {
            fractional.destroy();
        }
        if let Some(viewport) = self.viewport.take() {
            viewport.destroy();
        }
        self.dec.destroy();
        self.top.destroy();
        self.xdg.destroy();
        self.surface.destroy();
    }
}
fn connect(path: &Path, deadline: Instant) -> io::Result<UnixStream> {
    let b = path.as_os_str().as_bytes();
    let mut a: libc::sockaddr_un = unsafe { std::mem::zeroed() };
    if b.is_empty() || b.contains(&0) || b.len() >= a.sun_path.len() {
        return Err(io::Error::other("invalid socket path"));
    }
    a.sun_family = libc::AF_UNIX as libc::sa_family_t;
    for (to, from) in a.sun_path.iter_mut().zip(b) {
        *to = *from as libc::c_char;
    }
    #[cfg(target_vendor = "apple")]
    {
        a.sun_len = std::mem::size_of::<libc::sockaddr_un>() as u8;
    }
    let fd = unsafe { libc::socket(libc::AF_UNIX, libc::SOCK_STREAM, 0) };
    if fd < 0 {
        return Err(io::Error::last_os_error());
    }
    let stream = unsafe { UnixStream::from_raw_fd(fd) };
    stream.set_nonblocking(true)?;
    if unsafe { libc::fcntl(fd, libc::F_SETFD, libc::FD_CLOEXEC) } < 0 {
        return Err(io::Error::last_os_error());
    }
    let r = unsafe {
        libc::connect(
            fd,
            (&a as *const libc::sockaddr_un).cast(),
            std::mem::size_of_val(&a) as libc::socklen_t,
        )
    };
    if r < 0 {
        let e = io::Error::last_os_error();
        if !matches!(
            e.raw_os_error(),
            Some(libc::EINPROGRESS) | Some(libc::EAGAIN)
        ) {
            return Err(e);
        }
        let mut p = libc::pollfd {
            fd,
            events: libc::POLLOUT,
            revents: 0,
        };
        let end = deadline.min(Instant::now() + Duration::from_secs(3));
        loop {
            if STOP.load(Ordering::Relaxed) || Instant::now() >= end {
                return Err(io::Error::new(
                    io::ErrorKind::TimedOut,
                    "socket connect deadline",
                ));
            }
            let n = unsafe { libc::poll(&mut p, 1, 50) };
            if n < 0 {
                let e = io::Error::last_os_error();
                if e.kind() != io::ErrorKind::Interrupted {
                    return Err(e);
                }
            }
            if n > 0 {
                break;
            }
        }
        let mut err = 0i32;
        let mut len = std::mem::size_of_val(&err) as libc::socklen_t;
        if unsafe {
            libc::getsockopt(
                fd,
                libc::SOL_SOCKET,
                libc::SO_ERROR,
                (&mut err as *mut i32).cast(),
                &mut len,
            )
        } < 0
        {
            return Err(io::Error::last_os_error());
        }
        if err != 0 {
            return Err(io::Error::from_raw_os_error(err));
        }
    }
    Ok(stream)
}
fn log(dir: &Path, name: &str) -> io::Result<File> {
    let file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .mode(0o600)
        .custom_flags(libc::O_CLOEXEC | libc::O_NOFOLLOW)
        .open(dir.join(name))?;
    file.set_permissions(std::fs::Permissions::from_mode(0o600))?;
    Ok(file)
}
fn private_dir(dir: &Path) -> io::Result<PathBuf> {
    let m = std::fs::symlink_metadata(dir)?;
    if !dir.is_absolute()
        || !m.is_dir()
        || m.uid() != unsafe { libc::geteuid() }
        || m.mode() & 0o7777 != 0o700
    {
        return Err(io::Error::other(
            "records must be an owned private 0700 directory, not a symlink",
        ));
    }
    Ok(dir.to_owned())
}
fn json_record(dir: &Path, name: &str, value: &serde_json::Value) -> Result<()> {
    let mut file = log(dir, name)?;
    serde_json::to_writer_pretty(&mut file, value)?;
    file.write_all(b"\n")?;
    file.sync_all()?;
    Ok(())
}
fn flush(conn: &Connection) -> Result<bool> {
    match conn.flush() {
        Ok(()) => Ok(false),
        Err(wayland_client::backend::WaylandError::Io(e))
            if e.kind() == io::ErrorKind::WouldBlock =>
        {
            Ok(true)
        }
        Err(e) => Err(e.into()),
    }
}

fn child_exec_facts(pty: &pty::Pty) -> serde_json::Value {
    serde_json::json!({
        "pre_exec_fd_check_observed":pty.pre_exec_fd_scan_bound().is_some(),
        "pre_exec_fd_scan_bound_exclusive":pty.pre_exec_fd_scan_bound(),
        "exec_status_pipe_eof_observed":pty.exec_status_pipe_eof_observed(),
        "shell_exec_checked":pty.exec_checked(),
        "inventory_scope":"pre_exec_only_not_after_exec_inventory"
    })
}
fn session(
    stream: UnixStream,
    instance: &str,
    deadline: Instant,
    dir: &Path,
    pty: &mut pty::Pty,
    rx: &mut File,
    tx: &mut File,
    status: &mut status::Status,
    audit: &mut serde_json::Value,
) -> Result<()> {
    let conn = Connection::from_socket(stream)?;
    let mut queue = conn.new_event_queue::<State>();
    let q = queue.handle();
    conn.display().get_registry(&q, ());
    conn.display().sync(&q, 0u8);
    let mut s = State::new(instance.to_owned());
    let mut window: Option<Window> = None;
    let mut buffer: Option<Buffer> = None;
    // The metrics and sizes of what is committed (`None` before the first buffer).
    let mut shown: Option<(scale::Metrics, scale::Layout)> = None;
    let mut seq = 0;
    // SHM backing: what may be used, what the buffer on screen uses, why
    // POSIX shared memory was not used (if it was tried), and buffers made.
    let shm_policy = shm::policy();
    let mut shm_kind: Option<shm::Kind> = None;
    let mut shm_fallback: Option<String> = None;
    let (mut shm_posix, mut shm_file) = (0u32, 0u32);
    let mut grid = term::Term::new(80, 24)?;
    let mut renderer = render::Renderer::new()?;
    // The window's pixels, kept between frames: only changed rows are redrawn.
    let mut pixels: Vec<u8> = Vec::new();
    // The live state file (for unattended checks): what was last written, and when.
    let mut live = (String::new(), Instant::now() - Duration::from_secs(1));
    // Clipboard: the text last selected, a paste on its way in, and counts.
    let mut selected: Option<String> = None;
    let mut incoming: Option<clipboard::Incoming> = None;
    let (mut copies, mut pastes, mut pasted_bytes) = (0u32, 0u32, 0usize);
    let started = Instant::now();
    let mut received = 0;
    let mut checked_child_recorded = false;
    let result = (|| -> Result<()> {
        while Instant::now() < deadline && !STOP.load(Ordering::Relaxed) && !s.closed {
            status.pump()?;
            pty.setup()?;
            if pty.exec_checked() && !checked_child_recorded {
                let mut child = log(dir, "child-ready.json")?;
                serde_json::to_writer_pretty(
                    &mut child,
                    &serde_json::json!({"parent_pid":std::process::id(),"child_pid":pty.pid(),"sid":pty.pid(),"pgid":pty.pid(),"session_and_ctty_checked_in_child":true,"exec_status_pipe_closed":true,"exec_boundary":child_exec_facts(pty),"shell":"/bin/sh"}),
                )?;
                child.write_all(b"\n")?;
                child.sync_all()?;
                checked_child_recorded = true;
            }
            queue.dispatch_pending(&mut s)?;
            if let Some(e) = &s.error {
                return Err(e.clone().into());
            }
            if let Some(e) = s.ssd.error() {
                return Err(e.into());
            }
            if s.registry_done && window.is_none() {
                window = Some(Window::new(&mut s, &q)?);
            }
            if !s.ready && started.elapsed() > Duration::from_secs(20) {
                return Err("PTY/keymap/SSD/frame readiness deadline".into());
            }
            // Bound the work per turn, so an output flood cannot starve Wayland/signals.
            for _ in 0..16 {
                let mut b = [0; 4096];
                match pty.read(&mut b) {
                    Ok(0) => break,
                    Ok(n) => {
                        received += n;
                        rx.write_all(&b[..n])?;
                        if received > MAX_RX {
                            return Err("PTY output quota".into());
                        }
                        grid.feed(&b[..n]);
                        // Answers to the program's queries (cursor position, attributes).
                        let replies = grid.take_replies();
                        if !replies.is_empty() {
                            s.input.send(&replies)?;
                        }
                        s.dirty = true;
                    }
                    Err(e)
                        if matches!(
                            e.kind(),
                            io::ErrorKind::WouldBlock | io::ErrorKind::Interrupted
                        ) =>
                    {
                        break
                    }
                    Err(e) => return Err(e.into()),
                }
            }
            if pty.eof {
                break;
            }
            // Keys go out in the cursor-key mode the program asked for.
            s.input.application_cursor = grid.application_cursor();
            // Scrollback: typing returns to the live screen; Shift+PageUp/Down
            // move by pages; the wheel by three lines a detent. On the
            // alternate screen there is no history, so the wheel becomes
            // cursor keys for the full-screen program instead.
            if std::mem::take(&mut s.typed) && grid.scroll_to_bottom() {
                s.dirty = true;
            }
            let pages = std::mem::take(&mut s.scroll_pages);
            if pages != 0 && grid.scroll_view(pages as isize * (grid.rows as isize - 1).max(1)) {
                s.dirty = true;
            }
            let lines = scale::wheel_lines(&mut s.wheel);
            if lines != 0 {
                if grid.alternate_screen() {
                    let key: &[u8] = match (lines > 0, grid.application_cursor()) {
                        (true, true) => b"\x1bOA",
                        (true, false) => b"\x1b[A",
                        (false, true) => b"\x1bOB",
                        (false, false) => b"\x1b[B",
                    };
                    for _ in 0..lines.unsigned_abs().min(30) {
                        s.input.send(key)?;
                    }
                } else if grid.scroll_view(lines) {
                    s.dirty = true;
                }
            }
            // Selecting with the pointer. The text is taken when the drag
            // ends, so it stays copyable after output moved the highlight away.
            if let Some(range) = s.select.take() {
                if grid.select(range) {
                    s.dirty = true;
                }
            }
            if std::mem::take(&mut s.select_done) {
                if let Some(text) = grid.selected_text().filter(|t| !t.is_empty()) {
                    selected = Some(text);
                }
            }
            if s.data_device.is_none() {
                if let (Some(manager), Some(seat)) = (&s.data_manager, &s.seat) {
                    s.data_device = Some(manager.get_data_device(seat, &q, ()));
                }
            }
            // Copy: offer the selected text as the seat's selection.
            if std::mem::take(&mut s.input.copy_requested) {
                if let (Some(text), Some(manager), Some(device)) = (&selected, &s.data_manager, &s.data_device) {
                    let source = manager.create_data_source(&q, ());
                    for mime in clipboard::MIMES {
                        source.offer(mime.to_string());
                    }
                    device.set_selection(Some(&source), s.last_serial);
                    if let Some(old) = s.source.replace(source) {
                        old.destroy();
                    }
                    s.clip_text = text.clone();
                    copies += 1;
                }
            }
            // Paste: ask the selection's owner for text; it arrives through a pipe.
            if std::mem::take(&mut s.input.paste_requested) && incoming.is_none() {
                let offer = s.clipboard.as_ref().and_then(|c| s.offers.iter().find(|(o, _)| o == c));
                if let Some((offer, types)) = offer {
                    if let Some(mime) = clipboard::pick(types) {
                        let pipe = clipboard::Incoming::new()?;
                        if let Some(write) = pipe.write_end() {
                            offer.receive(mime.to_string(), write.as_fd());
                        }
                        incoming = Some(pipe);
                    }
                }
            }
            if let Some(text) = incoming.as_mut().and_then(|pipe| pipe.pump()) {
                incoming = None;
                let bytes = clipboard::paste_bytes(&text, grid.bracketed_paste());
                // A paste that does not fit the input quota is not typed in part.
                if !text.is_empty() && s.input.accepted + bytes.len() <= protocol::MAX_TX {
                    s.input.send(&bytes)?;
                    pastes += 1;
                    pasted_bytes += text.len().min(clipboard::MAX_PASTE);
                    if grid.scroll_to_bottom() {
                        s.dirty = true;
                    }
                }
            }
            s.outgoing.retain_mut(|out| !out.pump());
            // The state file: what the screen shows, for checks from outside.
            if live.1.elapsed() >= Duration::from_millis(200) {
                let now = format!(
                    "{} selected_bytes={} copies={copies} pastes={pastes} pasted_bytes={pasted_bytes} clipboard={}{}\n{}\n",
                    grid.state_line(),
                    selected.as_ref().map_or(0, |t| t.len()),
                    if s.data_device.is_some() { "available" } else { "absent" },
                    shown.as_ref().map_or(String::new(), |(m, l)| format!(
                        " scale120={} cell={}x{} buffer={}x{} logical={}x{}{}",
                        m.scale120, m.cw, m.ch, l.buffer.0, l.buffer.1, l.logical.0, l.logical.1,
                        shm_kind.map_or(String::new(), |kind| format!(" shm={}", kind.name()))
                    )),
                    grid.text()
                );
                if now != live.0 {
                    let tmp = dir.join("term-live.txt.tmp");
                    std::fs::write(&tmp, &now)?;
                    std::fs::rename(&tmp, dir.join("term-live.txt"))?;
                    live = (now, Instant::now());
                }
            }
            if s.frames.can_draw(s.ssd.ready()) && s.dirty {
                // The cell metrics at the effective scale, and the grid and
                // sizes for the configured logical size at those metrics. A
                // new scale alone (same configure) lands here too.
                let metrics = scale::Metrics::new(s.base_px, s.scale120());
                let layout = scale::layout(&metrics, s.configured, (grid.cols, grid.rows), s.fractional_mode);
                if shown != Some((metrics, layout)) {
                    let w = window.as_mut().ok_or("no configured window")?;
                    if buffer.as_ref().is_none_or(|b| b.size != layout.buffer) {
                        buffer.take();
                        seq += 1;
                        let new = Buffer::new(
                            s.shm.as_ref().ok_or("no shm")?,
                            &q,
                            dir,
                            layout.buffer,
                            seq,
                            metrics.scale120,
                            shm_policy,
                        )?;
                        let kind = new.backing.kind();
                        match kind {
                            shm::Kind::Posix => shm_posix += 1,
                            shm::Kind::File => shm_file += 1,
                        }
                        shm_kind = Some(kind);
                        if let Some(why) = new.backing.fallback() {
                            // Said once per buffer; a closed stderr must not stop the terminal.
                            let _ = writeln!(io::stderr(), "TERMSHM {}: {why}; using a file in the records directory", new.backing.what());
                            shm_fallback = Some(format!("{}: {why}", new.backing.what()));
                        }
                        buffer = Some(new);
                        pixels = vec![0u8; layout.buffer.0 * layout.buffer.1 * 4];
                    }
                    grid.resize(layout.cols, layout.rows)?;
                    // TIOCSWINSZ; the kernel sends the foreground program SIGWINCH.
                    pty.resize(layout.cols as u16, layout.rows as u16)?;
                    // Glyphs are rasterised again at the new pixel size.
                    renderer.set_metrics(metrics);
                    renderer.invalidate();
                    // Surface state for the commit below. With a viewport the
                    // buffer is device pixels at buffer scale 1 and the
                    // destination is the logical size; without, the buffer
                    // is the logical size times the whole scale.
                    if w.buffer_scale != layout.buffer_scale {
                        w.surface.set_buffer_scale(layout.buffer_scale);
                        w.buffer_scale = layout.buffer_scale;
                    }
                    if let Some(viewport) = &w.viewport {
                        viewport.set_destination(layout.logical.0 as i32, layout.logical.1 as i32);
                    }
                    if s.metrics != metrics {
                        Window::limits(&w.top, &metrics);
                    }
                    s.metrics = metrics;
                    s.logical = layout.logical;
                    shown = Some((metrics, layout));
                    // One line per change, for unattended checks of the scale.
                    // A closed or full stderr must not stop the terminal.
                    let _ = writeln!(
                        io::stderr(),
                        "{}{}",
                        scale::status_line(&metrics, &layout),
                        shm_kind.map_or(String::new(), |kind| format!(" shm={}", kind.name()))
                    );
                }
                let b = buffer.as_mut().unwrap();
                let size = b.size;
                // Only rows that changed are drawn, written and damaged.
                if let Some((y0, y1)) = renderer.draw(&grid, &mut pixels, size.0, size.1) {
                    let row = size.0 * 4;
                    b.backing.write(y0 * row, &pixels[y0 * row..y1 * row])?;
                    let w = window.as_ref().ok_or("no configured window")?;
                    // Window geometry is in logical units; damage in buffer pixels.
                    w.xdg
                        .set_window_geometry(0, 0, s.logical.0 as i32, s.logical.1 as i32);
                    s.frames.commit(s.ssd.ready(), received)?;
                    w.surface.frame(&q, 1u8);
                    w.surface.attach(Some(&b.buffer), 0, 0);
                    w.surface
                        .damage_buffer(0, y0 as i32, size.0 as i32, (y1 - y0) as i32);
                    w.surface.commit();
                    s.ack_uncommitted = false;
                } else if s.ack_uncommitted {
                    // A configure that changes nothing here (the same grid
                    // fits the new size) still has to be answered: without a
                    // commit the compositor waits for one, and the window's
                    // frame stays at the size it asked for, not ours.
                    if let Some(w) = window.as_ref() {
                        w.surface.commit();
                    }
                    s.ack_uncommitted = false;
                }
                s.dirty = false;
            }
            if !s.ready
                && pty.exec_checked()
                && s.input.keys.ready()
                && s.ssd.ready()
                && s.frames.callbacks > 0
            {
                s.ready = true;
                status.ready()?;
            }
            if pty.ready() {
                s.input.write_pending(|bytes| pty.write(bytes), &mut *tx)?;
            }
            if let Some(guard) = queue.prepare_read() {
                let writable = flush(&conn)?;
                // The paste request (and the pipe end in it) has left: only
                // the selection's owner holds the write end now.
                if !writable {
                    if let Some(pipe) = incoming.as_mut() {
                        pipe.sent();
                    }
                }
                let mut fds = [
                    libc::pollfd {
                        fd: conn.as_fd().as_raw_fd(),
                        events: libc::POLLIN | if writable { libc::POLLOUT } else { 0 },
                        revents: 0,
                    },
                    libc::pollfd {
                        fd: pty.fd(),
                        events: libc::POLLIN
                            | if s.input.pending_len() == 0 {
                                0
                            } else {
                                libc::POLLOUT
                            },
                        revents: 0,
                    },
                ];
                let n = unsafe { libc::poll(fds.as_mut_ptr(), 2, 20) };
                if n < 0 {
                    let e = io::Error::last_os_error();
                    if e.kind() != io::ErrorKind::Interrupted {
                        return Err(e.into());
                    }
                }
                if fds[0].revents & (libc::POLLHUP | libc::POLLERR | libc::POLLNVAL) != 0 {
                    return Err("Wayland disconnect".into());
                }
                if fds[0].revents & libc::POLLIN != 0 {
                    guard.read()?;
                } // otherwise drop cancels prepared read
            }
        }
        Ok(())
    })();
    // Destroy decoration before toplevel; server keeps any attached SHM fd alive.
    window.take();
    buffer.take();
    let _ = flush(&conn);
    let stop_cause = if result.is_err() {
        "fault"
    } else if s.closed {
        "window_close"
    } else if pty.eof {
        "shell_eof"
    } else if STOP.load(Ordering::Relaxed) {
        "signal"
    } else {
        "deadline"
    };
    *audit = serde_json::json!({"configure_serial":s.ssd.serial(),"ready":s.ready,"keymap":s.input.keys.ready(),"pty_exec_checked":pty.exec_checked(),"exec_boundary":child_exec_facts(pty),"ssd":s.ssd.ready(),"frames":s.frames.callbacks,"commits":s.frames.commits,"pty_rx_bytes":received,"pty_tx_bytes":s.input.written,"input_accepted_bytes":s.input.accepted,"input_unsent_bytes":s.input.pending_len(),"unsupported_terminal_sequences":grid.unsupported(),"scrollback_lines":grid.scrollback_len(),"clipboard_copies":copies,"clipboard_pastes":pastes,"clipboard_pasted_bytes":pasted_bytes,"alternate_screen":grid.alternate_screen(),"scale":shown.as_ref().map(|(m, l)| serde_json::json!({"scale120":m.scale120,"font_px":m.font_px(),"cell":[m.cw,m.ch],"grid":[l.cols,l.rows],"buffer":[l.buffer.0,l.buffer.1],"logical":[l.logical.0,l.logical.1],"buffer_scale":l.buffer_scale,"fractional_protocol":s.fractional_mode})),"shm":{"backing":shm_kind.map(shm::Kind::name),"fallback":shm_fallback,"posix_buffers":shm_posix,"file_buffers":shm_file},"stop_cause":stop_cause,"term_signal":STOP.load(Ordering::Relaxed),"window_closed":s.closed,"pty_eof":pty.eof});
    // Preserve a protocol failure even if writing the final grid also fails.
    let grid_result = (|| -> Result<()> {
        let mut final_grid = log(dir, "grid-final.txt")?;
        final_grid.write_all(grid.text().as_bytes())?;
        final_grid.sync_all()?;
        Ok(())
    })();
    result.and(grid_result)
}
fn arguments(args: &[String]) -> Result<(PathBuf, String, PathBuf, u64)> {
    if args.len() != 8
        || args[0] != "--socket"
        || args[2] != "--instance"
        || args[4] != "--records"
        || args[6] != "--seconds"
    {
        return Err("expected --socket PATH --instance HEX48 --records PATH --seconds N".into());
    }
    let socket = PathBuf::from(&args[1]);
    let dir = PathBuf::from(&args[5]);
    let seconds: u64 = args[7].parse()?;
    if !socket.is_absolute()
        || !dir.is_absolute()
        || socket.as_os_str().as_bytes().len() > 103
        || dir.as_os_str().as_bytes().len() > 1024
        || !status::valid_instance(&args[3])
        // Four hours: the desktop session's own upper bound.
        || !(1..=14_400).contains(&seconds)
    {
        return Err(
            "invalid socket, records path, lowercase instance identity, or lifetime".into(),
        );
    }
    Ok((socket, args[3].clone(), dir, seconds))
}
fn run() -> Result<()> {
    let started = Instant::now();
    let (socket, instance, dir, seconds) =
        arguments(&std::env::args().skip(1).collect::<Vec<_>>())?;
    if !cfg!(all(target_os = "macos", target_arch = "aarch64")) {
        return Err(
            "production terminal requires aarch64-apple-darwin; host tests do not qualify Darwin"
                .into(),
        );
    }
    signals()?;
    // The session explicitly assigns fd3. Adopt before any open can reuse it.
    if unsafe { libc::fcntl(3, libc::F_GETFD) } < 0 {
        return Err("missing inherited private status fd3".into());
    }
    let mut status = status::Status::adopt(unsafe { OwnedFd::from_raw_fd(3) }, &instance)?;
    let dir = private_dir(&dir)?;
    json_record(
        &dir,
        "application.json",
        &serde_json::json!({"schema":1,"instance":instance,"app_pid":std::process::id(),"socket":socket,"seconds":seconds,"shell":"/bin/sh","argv":["sh","-i","+m"],"term":term::TERM_NAME}),
    )?;
    let deadline = started + Duration::from_secs(seconds);
    let mut pty = None;
    let mut rx = None;
    let mut tx = None;
    let mut audit = serde_json::json!({"ready":false,"stop_cause":"setup_fault","input_accepted_bytes":0,"pty_tx_bytes":0,"input_unsent_bytes":0});
    let result = (|| -> Result<()> {
        rx = Some(log(&dir, "pty-rx.bin")?);
        tx = Some(log(&dir, "pty-tx.bin")?);
        // No fork after opening Wayland; the PTY child closes fd3 and all logs.
        pty = Some(pty::Pty::shell(80, 24)?);
        let child = pty.as_mut().unwrap();
        json_record(
            &dir,
            "child.json",
            &serde_json::json!({"parent_pid":std::process::id(),"child_pid":child.pid(),"setup_checked":false,"job_control":"disabled; foreground group must remain owned shell"}),
        )?;
        let stream = connect(&socket, deadline)?;
        session(
            stream,
            &instance,
            deadline,
            &dir,
            child,
            rx.as_mut().unwrap(),
            tx.as_mut().unwrap(),
            &mut status,
            &mut audit,
        )
    })();
    // Always cleanup before any fallible transcript/final-record operation.
    let child_pid = pty.as_ref().map(pty::Pty::pid);
    let mut cleanup = pty
        .as_mut()
        .map_or_else(pty::Cleanup::default, pty::Pty::cleanup);
    let mut errors = Vec::<String>::new();
    if let Err(error) = &result {
        errors.push(error.to_string());
    }
    if !cleanup.complete {
        errors.push("child cleanup incomplete; outer supervisor required".into());
    }
    for file in [rx.as_mut(), tx.as_mut()].into_iter().flatten() {
        if let Err(error) = file.sync_all() {
            errors.push(format!("transcript sync: {error}"));
            cleanup.complete = false;
        }
    }
    let record = serde_json::json!({"schema":1,"instance":instance,"app_pid":std::process::id(),"shell":"/bin/sh","term":term::TERM_NAME,"child_pid":child_pid,"child_exec_boundary":pty.as_ref().map(child_exec_facts),"session":audit,"cleanup":{"complete":cleanup.complete,"status":cleanup.status,"term_sent":cleanup.term_sent,"kill_sent":cleanup.kill_sent,"error":cleanup.error},"errors":errors});
    if let Err(error) = json_record(&dir, "result.json", &record) {
        errors.push(format!("final record: {error}"));
        cleanup.complete = false;
    }
    if let Err(error) = status.cleanup(&cleanup).and_then(|()| status.finish()) {
        errors.push(format!("status channel: {error}"));
        let _ = json_record(
            &dir,
            "status-error.json",
            &serde_json::json!({"error":error.to_string()}),
        );
    }
    if errors.is_empty() {
        Ok(())
    } else {
        Err(errors.join("; ").into())
    }
}
fn main() {
    if let Err(e) = run() {
        eprintln!("chonk-terminal-darwin: {e}");
        std::process::exit(1);
    }
}

#[cfg(test)]
mod lifecycle_tests {
    use super::*;
    use std::os::unix::fs::DirBuilderExt;
    #[test]
    fn explicit_cli_lowercase_identity_and_finite_bound() {
        let args = vec![
            "--socket".into(),
            "/tmp/wl".into(),
            "--instance".into(),
            "a".repeat(48),
            "--records".into(),
            "/tmp/private".into(),
            "--seconds".into(),
            "120".into(),
        ];
        assert!(arguments(&args).is_ok());
        for (index, value) in [
            (1, "relative"),
            (3, &"A".repeat(48)),
            (5, "relative"),
            (7, "0"),
            (7, "14401"),
        ] {
            let mut invalid = args.clone();
            invalid[index] = value.into();
            assert!(arguments(&invalid).is_err());
        }
        assert!(arguments(&[]).is_err());
    }
    #[test]
    fn two_private_instance_directories_files_and_collisions() {
        let stamp = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let parent = std::env::temp_dir().join(format!(
            "terminal-app-private-{}-{stamp}",
            std::process::id()
        ));
        std::fs::DirBuilder::new()
            .mode(0o700)
            .create(&parent)
            .unwrap();
        for name in ["a".repeat(48), "b".repeat(48)] {
            let dir = parent.join(name);
            std::fs::DirBuilder::new().mode(0o700).create(&dir).unwrap();
            assert_eq!(private_dir(&dir).unwrap(), dir);
            let file = log(&dir, "application.json").unwrap();
            assert_eq!(file.metadata().unwrap().mode() & 0o777, 0o600);
            assert!(log(&dir, "application.json").is_err());
            std::fs::set_permissions(&dir, std::fs::Permissions::from_mode(0o755)).unwrap();
            assert!(private_dir(&dir).is_err());
            std::fs::set_permissions(&dir, std::fs::Permissions::from_mode(0o700)).unwrap();
        }
        let link = parent.join("link");
        std::os::unix::fs::symlink(parent.join("a".repeat(48)), &link).unwrap();
        assert!(private_dir(&link).is_err());
        std::fs::remove_dir_all(parent).unwrap();
    }
    #[test]
    fn host_wayland_disconnect_preserves_pty_cleanup() {
        // Anonymous host socketpair: no listener, network, or guest compositor.
        let stamp = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let path =
            std::env::temp_dir().join(format!("dl11-disconnect-{}-{stamp}", std::process::id()));
        std::fs::DirBuilder::new()
            .mode(0o700)
            .create(&path)
            .unwrap();
        let (stream, peer) = UnixStream::pair().unwrap();
        stream.set_nonblocking(true).unwrap();
        drop(peer);
        let mut rx = log(&path, "rx").unwrap();
        let mut tx = log(&path, "tx").unwrap();
        let pid;
        {
            let mut p = pty::Pty::shell(80, 24).unwrap();
            pid = p.pid();
            let (channel, _peer) = UnixStream::pair().unwrap();
            let mut status = status::Status::adopt(channel.into(), &"0".repeat(48)).unwrap();
            let mut audit = serde_json::Value::Null;
            assert!(session(
                stream,
                &"0".repeat(48),
                Instant::now() + Duration::from_secs(1),
                &path,
                &mut p,
                &mut rx,
                &mut tx,
                &mut status,
                &mut audit
            )
            .is_err());
            let cleanup = p.cleanup();
            assert!(cleanup.complete, "{cleanup:?}");
            assert_eq!(p.cleanup().status, cleanup.status);
        }
        let mut status = 0;
        assert_eq!(
            unsafe { libc::waitpid(pid, &mut status, libc::WNOHANG) },
            -1
        );
        std::fs::remove_dir_all(path).unwrap();
    }
}
