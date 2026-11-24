//! Real host Wayland server/client and PTYs; all input below is synthetic.
use super::*;
use std::{
    os::unix::fs::DirBuilderExt,
    sync::{atomic::AtomicBool, Arc},
};
use wayland_protocols::wp::{
    fractional_scale::v1::server::{wp_fractional_scale_manager_v1 as fm, wp_fractional_scale_v1 as fs},
    viewporter::server::{wp_viewport as vport, wp_viewporter as vp},
};
use wayland_protocols::xdg::{
    decoration::zv1::server::{
        zxdg_decoration_manager_v1 as dm, zxdg_toplevel_decoration_v1 as dec,
    },
    shell::server::{xdg_surface as xs, xdg_toplevel as xt, xdg_wm_base as wm},
};
use wayland_server::protocol::{
    wl_buffer as sb, wl_callback as cb, wl_compositor as sc, wl_keyboard as kb, wl_output as so,
    wl_pointer as ptr, wl_region as sr, wl_seat as seat, wl_shm as shm, wl_shm_pool as pool,
    wl_surface as ss,
};
use wayland_server::{Client, DataInit, Display, DisplayHandle, GlobalDispatch, New, Resource};

#[derive(Clone, Copy, PartialEq)]
enum Mode {
    Unfocused,
    HeldLaunch,
    TypeLeave,
    CloseBeforeReady,
    EofBeforeReady,
}
struct Server {
    mode: Mode,
    surface: Option<ss::WlSurface>,
    xdg: Option<xs::XdgSurface>,
    top: Option<xt::XdgToplevel>,
    keyboard: Option<kb::WlKeyboard>,
    configured: bool,
    buffer: Option<sb::WlBuffer>,
    callbacks: Vec<cb::WlCallback>,
    keymap: File,
    keymap_len: u32,
    commits: usize,
    saw_ready: bool,
    closed: bool,
    /// Output scale fixtures (`None`: the original compositor without any of it).
    case: Option<Case>,
    output: Option<so::WlOutput>,
    fractional: Option<fs::WpFractionalScaleV1>,
    pointer: Option<ptr::WlPointer>,
    /// What the client said: buffer sizes, pending surface state, and the
    /// state of every commit that carried a buffer.
    sizes: Vec<(sb::WlBuffer, (i32, i32))>,
    /// Every pool the client created: its descriptor and size.
    pools: Vec<(std::os::fd::OwnedFd, i32)>,
    buffer_scale: i32,
    buffer_scale_requests: Vec<i32>,
    destination: Option<(i32, i32)>,
    geometry: Option<(i32, i32, i32, i32)>,
    limits: Vec<(i32, i32)>,
    damage: (usize, usize),
    frames: Vec<Frame>,
}
/// One output-scale scenario of the fixture compositor.
#[derive(Clone, Copy, PartialEq, Debug)]
struct Case {
    /// wl_compositor version (6 has preferred_buffer_scale).
    compositor: u32,
    /// Advertise wp_fractional_scale_manager_v1; the preferred scale sent at once.
    fractional: Option<u32>,
    viewporter: bool,
    /// wl_surface.preferred_buffer_scale, sent with the first configure.
    preferred: Option<i32>,
    /// Advertise a wl_output with this scale; the surface enters it.
    output: Option<i32>,
    /// The first toplevel configure (0: the client chooses).
    configure: (i32, i32),
    /// After READY: this scale (120ths) through the first source in use,
    /// then a configure of the logical size the client committed.
    then: Option<u32>,
}
/// The state of one commit with a buffer.
#[derive(Clone, Copy, PartialEq, Debug)]
struct Frame {
    buffer: (i32, i32),
    buffer_scale: i32,
    destination: Option<(i32, i32)>,
    geometry: Option<(i32, i32, i32, i32)>,
}
macro_rules! global {
    ($ty:ty) => {
        impl GlobalDispatch<$ty, ()> for Server {
            fn bind(
                _: &mut Self,
                _: &DisplayHandle,
                _: &Client,
                resource: New<$ty>,
                _: &(),
                init: &mut DataInit<'_, Self>,
            ) {
                init.init(resource, ());
            }
        }
    };
}
global!(sc::WlCompositor);
global!(shm::WlShm);
global!(wm::XdgWmBase);
global!(dm::ZxdgDecorationManagerV1);
global!(fm::WpFractionalScaleManagerV1);
global!(vp::WpViewporter);
impl GlobalDispatch<seat::WlSeat, ()> for Server {
    fn bind(
        s: &mut Self,
        _: &DisplayHandle,
        _: &Client,
        r: New<seat::WlSeat>,
        _: &(),
        init: &mut DataInit<'_, Self>,
    ) {
        let seat = init.init(r, ());
        // The scale scenarios also point, drag and scroll.
        seat.capabilities(if s.case.is_some() {
            seat::Capability::Keyboard | seat::Capability::Pointer
        } else {
            seat::Capability::Keyboard
        });
    }
}
impl GlobalDispatch<so::WlOutput, ()> for Server {
    fn bind(
        s: &mut Self,
        _: &DisplayHandle,
        _: &Client,
        r: New<so::WlOutput>,
        _: &(),
        init: &mut DataInit<'_, Self>,
    ) {
        let output = init.init(r, ());
        output.scale(s.case.and_then(|c| c.output).unwrap_or(1));
        output.done();
        s.output = Some(output);
    }
}
impl wayland_server::Dispatch<fm::WpFractionalScaleManagerV1, ()> for Server {
    fn request(
        s: &mut Self,
        _: &Client,
        _: &fm::WpFractionalScaleManagerV1,
        r: fm::Request,
        _: &(),
        _: &DisplayHandle,
        i: &mut DataInit<'_, Self>,
    ) {
        if let fm::Request::GetFractionalScale { id, .. } = r {
            let fractional = i.init(id, ());
            if let Some(scale) = s.case.and_then(|c| c.fractional) {
                fractional.preferred_scale(scale);
            }
            s.fractional = Some(fractional);
        }
    }
}
impl wayland_server::Dispatch<vp::WpViewporter, ()> for Server {
    fn request(
        _: &mut Self,
        _: &Client,
        _: &vp::WpViewporter,
        r: vp::Request,
        _: &(),
        _: &DisplayHandle,
        i: &mut DataInit<'_, Self>,
    ) {
        if let vp::Request::GetViewport { id, .. } = r {
            i.init(id, ());
        }
    }
}
impl wayland_server::Dispatch<vport::WpViewport, ()> for Server {
    fn request(
        s: &mut Self,
        _: &Client,
        _: &vport::WpViewport,
        r: vport::Request,
        _: &(),
        _: &DisplayHandle,
        _: &mut DataInit<'_, Self>,
    ) {
        if let vport::Request::SetDestination { width, height } = r {
            s.destination = Some((width, height));
        }
    }
}
impl wayland_server::Dispatch<xt::XdgToplevel, ()> for Server {
    fn request(
        s: &mut Self,
        _: &Client,
        _: &xt::XdgToplevel,
        r: xt::Request,
        _: &(),
        _: &DisplayHandle,
        _: &mut DataInit<'_, Self>,
    ) {
        if let xt::Request::SetMinSize { width, height } = r {
            s.limits.push((width, height));
        }
    }
}
impl wayland_server::Dispatch<sc::WlCompositor, ()> for Server {
    fn request(
        s: &mut Self,
        _: &Client,
        _: &sc::WlCompositor,
        r: sc::Request,
        _: &(),
        _: &DisplayHandle,
        i: &mut DataInit<'_, Self>,
    ) {
        match r {
            sc::Request::CreateSurface { id } => s.surface = Some(i.init(id, ())),
            sc::Request::CreateRegion { id } => {
                i.init(id, ());
            }
            _ => (),
        }
    }
}
impl wayland_server::Dispatch<ss::WlSurface, ()> for Server {
    fn request(
        s: &mut Self,
        _: &Client,
        _: &ss::WlSurface,
        r: ss::Request,
        _: &(),
        _: &DisplayHandle,
        i: &mut DataInit<'_, Self>,
    ) {
        match r {
            ss::Request::Frame { callback } => s.callbacks.push(i.init(callback, ())),
            ss::Request::Attach { buffer, .. } => s.buffer = buffer,
            ss::Request::SetBufferScale { scale } => {
                s.buffer_scale = scale;
                s.buffer_scale_requests.push(scale);
            }
            ss::Request::Damage { .. } => s.damage.0 += 1,
            ss::Request::DamageBuffer { .. } => s.damage.1 += 1,
            ss::Request::Commit => {
                if !s.configured && s.xdg.is_some() {
                    s.configured = true;
                    let surface = s.surface.as_ref().unwrap();
                    if let (Some(output), Some(_)) = (&s.output, s.case.and_then(|c| c.output)) {
                        surface.enter(output);
                    }
                    if let Some(factor) = s.case.and_then(|c| c.preferred) {
                        surface.preferred_buffer_scale(factor);
                    }
                    let (w, h) = s.case.map_or((0, 0), |c| c.configure);
                    s.top.as_ref().unwrap().configure(w, h, vec![]);
                    s.xdg.as_ref().unwrap().configure(1);
                    if s.mode == Mode::CloseBeforeReady {
                        s.top.as_ref().unwrap().close();
                        s.closed = true;
                    }
                }
                if let Some(buffer) = s.buffer.take() {
                    s.commits += 1;
                    let size = s.sizes.iter().find(|(b, _)| *b == buffer).map(|(_, size)| *size);
                    s.frames.push(Frame {
                        buffer: size.unwrap_or((0, 0)),
                        buffer_scale: s.buffer_scale,
                        destination: s.destination,
                        geometry: s.geometry,
                    });
                    // This fixture acknowledges wire presentation only. It is
                    // not a compositor renderer or physical scanout witness.
                    buffer.release();
                    for callback in s.callbacks.drain(..) {
                        callback.done(1);
                    }
                }
            }
            _ => (),
        }
    }
}
impl wayland_server::Dispatch<shm::WlShm, ()> for Server {
    fn request(
        s: &mut Self,
        _: &Client,
        _: &shm::WlShm,
        r: shm::Request,
        _: &(),
        _: &DisplayHandle,
        i: &mut DataInit<'_, Self>,
    ) {
        if let shm::Request::CreatePool { id, fd, size } = r {
            i.init(id, ());
            s.pools.push((fd, size));
        }
    }
}
impl wayland_server::Dispatch<pool::WlShmPool, ()> for Server {
    fn request(
        s: &mut Self,
        _: &Client,
        _: &pool::WlShmPool,
        r: pool::Request,
        _: &(),
        _: &DisplayHandle,
        i: &mut DataInit<'_, Self>,
    ) {
        if let pool::Request::CreateBuffer { id, width, height, .. } = r {
            let buffer = i.init(id, ());
            s.sizes.push((buffer, (width, height)));
        }
    }
}
impl wayland_server::Dispatch<seat::WlSeat, ()> for Server {
    fn request(
        s: &mut Self,
        _: &Client,
        _: &seat::WlSeat,
        r: seat::Request,
        _: &(),
        _: &DisplayHandle,
        i: &mut DataInit<'_, Self>,
    ) {
        if let seat::Request::GetKeyboard { id } = r {
            let k = i.init(id, ());
            k.keymap(kb::KeymapFormat::XkbV1, s.keymap.as_fd(), s.keymap_len);
            s.keyboard = Some(k);
        } else if let seat::Request::GetPointer { id } = r {
            s.pointer = Some(i.init(id, ()));
        }
    }
}
impl wayland_server::Dispatch<wm::XdgWmBase, ()> for Server {
    fn request(
        s: &mut Self,
        _: &Client,
        _: &wm::XdgWmBase,
        r: wm::Request,
        _: &(),
        _: &DisplayHandle,
        i: &mut DataInit<'_, Self>,
    ) {
        if let wm::Request::GetXdgSurface { id, .. } = r {
            s.xdg = Some(i.init(id, ()));
        }
    }
}
impl wayland_server::Dispatch<xs::XdgSurface, ()> for Server {
    fn request(
        s: &mut Self,
        _: &Client,
        _: &xs::XdgSurface,
        r: xs::Request,
        _: &(),
        _: &DisplayHandle,
        i: &mut DataInit<'_, Self>,
    ) {
        if let xs::Request::GetToplevel { id } = r {
            s.top = Some(i.init(id, ()));
        } else if let xs::Request::SetWindowGeometry { x, y, width, height } = r {
            s.geometry = Some((x, y, width, height));
        }
    }
}
impl wayland_server::Dispatch<dm::ZxdgDecorationManagerV1, ()> for Server {
    fn request(
        _: &mut Self,
        _: &Client,
        _: &dm::ZxdgDecorationManagerV1,
        r: dm::Request,
        _: &(),
        _: &DisplayHandle,
        i: &mut DataInit<'_, Self>,
    ) {
        if let dm::Request::GetToplevelDecoration { id, .. } = r {
            i.init(id, ()).configure(dec::Mode::ServerSide);
        }
    }
}
macro_rules! ignore {
    ($ty:ty) => {
        impl wayland_server::Dispatch<$ty, ()> for Server {
            fn request(
                _: &mut Self,
                _: &Client,
                _: &$ty,
                _: <$ty as Resource>::Request,
                _: &(),
                _: &DisplayHandle,
                _: &mut DataInit<'_, Self>,
            ) {
            }
        }
    };
}
ignore!(sr::WlRegion);
ignore!(sb::WlBuffer);
ignore!(cb::WlCallback);
ignore!(kb::WlKeyboard);
ignore!(so::WlOutput);
ignore!(ptr::WlPointer);
ignore!(fs::WpFractionalScaleV1);
ignore!(dec::ZxdgToplevelDecorationV1);

fn run(mode: Mode) -> (serde_json::Value, Vec<u8>, String) {
    let stamp = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_nanos();
    let dir =
        std::env::temp_dir().join(format!("terminal-app-wire-{}-{stamp}", std::process::id()));
    std::fs::DirBuilder::new().mode(0o700).create(&dir).unwrap();
    let mut rx = log(&dir, "pty-rx.bin").unwrap();
    let mut tx = log(&dir, "pty-tx.bin").unwrap();
    // Fork before any Wayland connection or server fixture is opened.
    let mut pty = if mode == Mode::EofBeforeReady {
        pty::Pty::fixture("/bin/sh", &["sh", "-c", "exit 0"]).unwrap()
    } else {
        pty::Pty::shell(80, 24).unwrap()
    };
    let c = xkbcommon::xkb::Context::new(xkbcommon::xkb::CONTEXT_NO_FLAGS);
    let map = xkbcommon::xkb::Keymap::new_from_names(
        &c,
        "evdev",
        "pc105",
        "us",
        "",
        None,
        xkbcommon::xkb::KEYMAP_COMPILE_NO_FLAGS,
    )
    .unwrap();
    let mut bytes = map
        .get_as_string(xkbcommon::xkb::KEYMAP_FORMAT_TEXT_V1)
        .into_bytes();
    bytes.push(0);
    let mut keymap = OpenOptions::new()
        .read(true)
        .write(true)
        .create_new(true)
        .mode(0o600)
        .open(dir.join("map"))
        .unwrap();
    keymap.write_all(&bytes).unwrap();
    keymap.seek(SeekFrom::Start(0)).unwrap();
    let (client, peer) = UnixStream::pair().unwrap();
    client.set_nonblocking(true).unwrap();
    let (channel, mut status_peer) = UnixStream::pair().unwrap();
    status_peer.set_nonblocking(true).unwrap();
    let mut status = status::Status::adopt(channel.into(), &"a".repeat(48)).unwrap();
    let done = Arc::new(AtomicBool::new(false));
    let server_done = done.clone();
    let server_dir = dir.clone();
    let worker = std::thread::spawn(move || {
        let mut display = Display::<Server>::new().unwrap();
        let mut handle = display.handle();
        handle.create_global::<Server, sc::WlCompositor, _>(4, ());
        handle.create_global::<Server, shm::WlShm, _>(1, ());
        handle.create_global::<Server, seat::WlSeat, _>(5, ());
        handle.create_global::<Server, wm::XdgWmBase, _>(6, ());
        handle.create_global::<Server, dm::ZxdgDecorationManagerV1, _>(1, ());
        let _client = handle.insert_client(peer, Arc::new(())).unwrap();
        let mut s = Server {
            mode,
            surface: None,
            xdg: None,
            top: None,
            keyboard: None,
            configured: false,
            buffer: None,
            callbacks: Vec::new(),
            keymap,
            keymap_len: bytes.len() as u32,
            commits: 0,
            saw_ready: false,
            closed: false,
            case: None,
            output: None,
            fractional: None,
            pointer: None,
            sizes: Vec::new(),
            pools: Vec::new(),
            buffer_scale: 1,
            buffer_scale_requests: Vec::new(),
            destination: None,
            geometry: None,
            limits: Vec::new(),
            damage: (0, 0),
            frames: Vec::new(),
        };
        let mut held_sent = false;
        let mut messages = String::new();
        let until = Instant::now() + Duration::from_secs(4);
        while !server_done.load(Ordering::Relaxed) && Instant::now() < until {
            display.dispatch_clients(&mut s).unwrap();
            if mode == Mode::HeldLaunch && !held_sent && s.configured {
                if let Some(k) = s.keyboard.as_ref() {
                    k.enter(
                        2,
                        s.surface.as_ref().unwrap(),
                        [125u32, 28]
                            .into_iter()
                            .flat_map(u32::to_ne_bytes)
                            .collect(),
                    );
                    k.modifiers(3, 64, 0, 0, 0);
                    held_sent = true;
                }
            }
            display.flush_clients().unwrap();
            let mut buf = [0; 512];
            match status_peer.read(&mut buf) {
                Ok(n) => messages.push_str(std::str::from_utf8(&buf[..n]).unwrap()),
                Err(e) if e.kind() == io::ErrorKind::WouldBlock => (),
                Err(e) => panic!("{e}"),
            }
            if !s.saw_ready && messages.contains(" READY ") {
                s.saw_ready = true;
                if mode == Mode::HeldLaunch {
                    let k = s.keyboard.as_ref().unwrap();
                    k.key(4, 1, 28, kb::KeyState::Released);
                    k.key(5, 1, 125, kb::KeyState::Released);
                    k.modifiers(6, 0, 0, 0, 0);
                }
                if mode == Mode::TypeLeave {
                    let k = s.keyboard.as_ref().unwrap();
                    let surface = s.surface.as_ref().unwrap();
                    k.enter(7, surface, vec![]);
                    k.modifiers(8, 0, 0, 0, 0);
                    for key in [30, 48, 46] {
                        k.key(9, 1, key, kb::KeyState::Pressed);
                        k.key(10, 1, key, kb::KeyState::Released);
                    }
                    k.leave(11, surface);
                } else {
                    s.top.as_ref().unwrap().close();
                    s.closed = true;
                }
                display.flush_clients().unwrap();
            }
            if mode == Mode::TypeLeave
                && s.saw_ready
                && !s.closed
                && std::fs::metadata(server_dir.join("pty-tx.bin"))
                    .unwrap()
                    .len()
                    == 3
            {
                s.top.as_ref().unwrap().close();
                s.closed = true;
                display.flush_clients().unwrap();
            }
            std::thread::sleep(Duration::from_millis(1));
        }
        // Keep status peer alive until the client loop is done; this fixture
        // tests READY, while module tests exercise CLEANUP framing separately.
        (messages, s.saw_ready, s.commits)
    });
    let mut audit = serde_json::Value::Null;
    let result = session(
        client,
        &"a".repeat(48),
        Instant::now() + Duration::from_secs(3),
        &dir,
        &mut pty,
        &mut rx,
        &mut tx,
        &mut status,
        &mut audit,
    );
    done.store(true, Ordering::Relaxed);
    let (messages, ready, commits) = worker.join().unwrap();
    let cleanup = pty.cleanup();
    assert!(cleanup.complete, "{cleanup:?}");
    assert!(result.is_ok(), "{result:?} audit={audit}");
    if matches!(mode, Mode::Unfocused | Mode::HeldLaunch | Mode::TypeLeave) {
        assert!(ready);
        assert!(commits > 0);
        assert_eq!(audit["ready"], true);
    } else {
        assert!(!ready);
        assert_eq!(audit["ready"], false);
    }
    let transcript = std::fs::read(dir.join("pty-tx.bin")).unwrap();
    std::fs::remove_dir_all(dir).unwrap();
    (audit, transcript, messages)
}
#[test]
fn host_real_wayland_unfocused_ready_then_normal_close() {
    let (a, t, _) = run(Mode::Unfocused);
    assert_eq!(a["stop_cause"], "window_close");
    assert!(t.is_empty());
}
#[test]
fn host_real_wayland_synthetic_launch_chord_held_map_then_release() {
    let (_, t, _) = run(Mode::HeldLaunch);
    assert!(t.is_empty());
}
#[test]
fn host_real_wayland_synthetic_text_then_leave_reaches_real_pty() {
    let (a, t, _) = run(Mode::TypeLeave);
    assert_eq!(t, b"abc");
    assert_eq!(a["input_unsent_bytes"], 0);
}
#[test]
fn host_real_wayland_normal_close_before_ready() {
    let (a, t, m) = run(Mode::CloseBeforeReady);
    assert_eq!(a["stop_cause"], "window_close");
    assert!(t.is_empty());
    assert!(m.is_empty());
}
#[test]
fn host_real_pty_normal_eof_before_ready() {
    let (a, t, _) = run(Mode::EofBeforeReady);
    assert_eq!(a["stop_cause"], "shell_eof");
    assert!(t.is_empty());
}

/// What one scale scenario showed.
struct Seen {
    frames: Vec<Frame>,
    buffer_scale_requests: Vec<i32>,
    /// xdg_toplevel.set_min_size requests, in order.
    limits: Vec<(i32, i32)>,
    /// wl_surface.damage and damage_buffer requests.
    damage: (usize, usize),
    /// Bytes selected by the drag before and after the scale change.
    selected: (Option<usize>, Option<usize>),
    /// Scrollback offset after two wheel detents up.
    scrolled: Option<usize>,
    /// TIOCGWINSZ of the PTY at the end: columns, rows.
    winsize: (u16, u16),
    audit: serde_json::Value,
    /// How the session ended (`run_scale` requires success).
    result: std::result::Result<(), String>,
    /// The last pool the compositor was given, as its own mapping of the
    /// descriptor shows it after the client has gone; the PTY output; and
    /// the fields of the last `TERMSTATE` line.
    pool: Vec<u8>,
    rx: Vec<u8>,
    state: std::collections::HashMap<String, String>,
}
/// The live state file: its `key=value` fields and the screen's lines.
fn live(dir: &Path) -> Option<(std::collections::HashMap<String, String>, Vec<String>)> {
    let content = std::fs::read_to_string(dir.join("term-live.txt")).ok()?;
    let (state, text) = content.split_once('\n')?;
    let fields = state.split(' ').filter_map(|kv| kv.split_once('=')).map(|(k, v)| (k.to_string(), v.to_string())).collect();
    Some((fields, text.lines().map(str::to_string).collect()))
}
/// A compositor with output scaling: the client starts at `case`'s scale,
/// a drag selects text, the scale changes (if asked), another drag selects
/// other text, and the wheel scrolls back. Pointer positions are sent in
/// logical surface units, as a compositor does.
fn run_scale(case: Case) -> Seen {
    let seen = run_scale_any(case);
    assert!(seen.result.is_ok(), "{:?} audit={}", seen.result, seen.audit);
    assert_eq!(seen.audit["ready"], true);
    assert_eq!(seen.audit["stop_cause"], "window_close", "the scenario ran to its end: {}", seen.audit);
    seen
}
/// The same, whatever the outcome of the session.
fn run_scale_any(case: Case) -> Seen {
    let stamp = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_nanos();
    let dir =
        std::env::temp_dir().join(format!("terminal-app-scale-{}-{stamp}", std::process::id()));
    std::fs::DirBuilder::new().mode(0o700).create(&dir).unwrap();
    let mut rx = log(&dir, "pty-rx.bin").unwrap();
    let mut tx = log(&dir, "pty-tx.bin").unwrap();
    // 200 lines of history, a word to select on the last line, then idle.
    let script = "i=0; while [ $i -lt 200 ]; do echo L$i; i=$((i+1)); done; printf ABCDEFGHIJ; exec sleep 20";
    let mut pty = pty::Pty::fixture("/bin/sh", &["sh", "-c", script]).unwrap();
    let c = xkbcommon::xkb::Context::new(xkbcommon::xkb::CONTEXT_NO_FLAGS);
    let map = xkbcommon::xkb::Keymap::new_from_names(&c, "evdev", "pc105", "us", "", None, xkbcommon::xkb::KEYMAP_COMPILE_NO_FLAGS).unwrap();
    let mut bytes = map.get_as_string(xkbcommon::xkb::KEYMAP_FORMAT_TEXT_V1).into_bytes();
    bytes.push(0);
    let mut keymap = OpenOptions::new().read(true).write(true).create_new(true).mode(0o600).open(dir.join("map")).unwrap();
    keymap.write_all(&bytes).unwrap();
    keymap.seek(SeekFrom::Start(0)).unwrap();
    let (client, peer) = UnixStream::pair().unwrap();
    client.set_nonblocking(true).unwrap();
    let (channel, mut status_peer) = UnixStream::pair().unwrap();
    status_peer.set_nonblocking(true).unwrap();
    let mut status = status::Status::adopt(channel.into(), &"a".repeat(48)).unwrap();
    let done = Arc::new(AtomicBool::new(false));
    let server_done = done.clone();
    let server_dir = dir.clone();
    let worker = std::thread::spawn(move || {
        let mut display = Display::<Server>::new().unwrap();
        let mut handle = display.handle();
        handle.create_global::<Server, sc::WlCompositor, _>(case.compositor, ());
        handle.create_global::<Server, shm::WlShm, _>(1, ());
        handle.create_global::<Server, seat::WlSeat, _>(5, ());
        handle.create_global::<Server, wm::XdgWmBase, _>(6, ());
        handle.create_global::<Server, dm::ZxdgDecorationManagerV1, _>(1, ());
        if case.output.is_some() {
            handle.create_global::<Server, so::WlOutput, _>(4, ());
        }
        if case.fractional.is_some() {
            handle.create_global::<Server, fm::WpFractionalScaleManagerV1, _>(1, ());
        }
        if case.viewporter {
            handle.create_global::<Server, vp::WpViewporter, _>(1, ());
        }
        let _client = handle.insert_client(peer, Arc::new(())).unwrap();
        let mut s = Server {
            mode: Mode::Unfocused,
            surface: None,
            xdg: None,
            top: None,
            keyboard: None,
            configured: false,
            buffer: None,
            callbacks: Vec::new(),
            keymap,
            keymap_len: bytes.len() as u32,
            commits: 0,
            saw_ready: false,
            closed: false,
            case: Some(case),
            output: None,
            fractional: None,
            pointer: None,
            sizes: Vec::new(),
            pools: Vec::new(),
            buffer_scale: 1,
            buffer_scale_requests: Vec::new(),
            destination: None,
            geometry: None,
            limits: Vec::new(),
            damage: (0, 0),
            frames: Vec::new(),
        };
        // The scale the client must be showing, by the precedence rule.
        let first = scale::effective(case.fractional.filter(|_| case.viewporter), case.preferred.filter(|_| case.compositor >= 6), case.output);
        let (mut expected, mut step, mut entered) = (first, 0, false);
        let (mut selected, mut scrolled) = ((None, None), None);
        let mut messages = String::new();
        let until = Instant::now() + Duration::from_secs(8);
        while !server_done.load(Ordering::Relaxed) && Instant::now() < until && !s.closed {
            display.dispatch_clients(&mut s).unwrap();
            display.flush_clients().unwrap();
            let mut buf = [0; 512];
            match status_peer.read(&mut buf) {
                Ok(n) => messages.push_str(std::str::from_utf8(&buf[..n]).unwrap()),
                Err(e) if e.kind() == io::ErrorKind::WouldBlock => (),
                Err(e) => panic!("{e}"),
            }
            std::thread::sleep(Duration::from_millis(1));
            if !messages.contains(" READY ") {
                continue;
            }
            let Some((fields, text)) = live(&server_dir) else { continue };
            let number = |key: &str| fields.get(key).and_then(|v| v.parse::<usize>().ok());
            // Everything below waits for the client to show the expected scale and the word.
            let row = text.iter().position(|line| line.starts_with("ABCDEFGHIJ"));
            if number("scale120") != Some(expected as usize) {
                continue;
            }
            let pointer = s.pointer.clone().unwrap();
            let surface = s.surface.clone().unwrap();
            // The middle of a cell, in logical units, as the compositor would send it.
            let m = scale::Metrics::new(scale::BASE_FONT_PX, expected);
            let at = |col: usize, row: usize| {
                let f = f64::from(expected) / 120.0;
                (((m.pad + col * m.cw) as f64 + m.cw as f64 / 2.0) / f, ((m.pad + row * m.ch) as f64 + m.ch as f64 / 2.0) / f)
            };
            let mut drag = |from: usize, to: usize, row: usize| {
                let (a, b) = (at(from, row), at(to, row));
                if !std::mem::replace(&mut entered, true) {
                    pointer.enter(20, &surface, a.0, a.1);
                }
                pointer.motion(1, a.0, a.1);
                pointer.button(21, 2, 0x110, ptr::ButtonState::Pressed);
                pointer.motion(3, b.0, b.1);
                pointer.button(22, 4, 0x110, ptr::ButtonState::Released);
                pointer.frame();
            };
            match step {
                // Select "BCDE" (columns 1 to 4 of the word's row).
                0 => {
                    if let Some(row) = row {
                        drag(1, 4, row);
                        step = 1;
                    }
                }
                1 if number("selected_bytes") == Some(4) => {
                    selected.0 = Some(4);
                    match case.then {
                        Some(next) => {
                            if case.fractional.is_some() && case.viewporter {
                                s.fractional.as_ref().unwrap().preferred_scale(next);
                            } else if case.preferred.is_some() && case.compositor >= 6 {
                                surface.preferred_buffer_scale((next / 120) as i32);
                            } else {
                                let output = s.output.as_ref().unwrap();
                                output.scale((next / 120) as i32);
                                output.done();
                            }
                            // The compositor adopted the committed logical size and configures it again.
                            let (_, _, w, h) = s.geometry.unwrap();
                            s.top.as_ref().unwrap().configure(w, h, vec![]);
                            s.xdg.as_ref().unwrap().configure(2);
                            expected = next;
                            step = 2;
                        }
                        None => step = 4,
                    }
                }
                // At the new scale: select "CDEFGH" (columns 2 to 7).
                2 => {
                    if let Some(row) = row {
                        drag(2, 7, row);
                        step = 3;
                    }
                }
                3 if number("selected_bytes") == Some(6) => {
                    selected.1 = Some(6);
                    step = 4;
                }
                // Two detents up: axis values are logical, 10 a detent.
                4 => {
                    if !std::mem::replace(&mut entered, true) {
                        pointer.enter(20, &surface, 1.0, 1.0);
                    }
                    pointer.axis(5, ptr::Axis::VerticalScroll, -20.0);
                    pointer.frame();
                    step = 5;
                }
                5 if number("scrollback_offset").is_some_and(|offset| offset != 0) => {
                    scrolled = number("scrollback_offset");
                    s.top.as_ref().unwrap().close();
                    s.closed = true;
                }
                _ => {}
            }
            display.flush_clients().unwrap();
        }
        // Let the close reach the client; keep the status peer until it is done.
        let until = Instant::now() + Duration::from_secs(2);
        while !server_done.load(Ordering::Relaxed) && Instant::now() < until {
            display.dispatch_clients(&mut s).unwrap();
            display.flush_clients().unwrap();
            std::thread::sleep(Duration::from_millis(1));
        }
        drop(status_peer);
        (s.frames, s.buffer_scale_requests, s.limits, s.damage, selected, scrolled, s.pools)
    });
    let mut audit = serde_json::Value::Null;
    let result = session(client, &"a".repeat(48), Instant::now() + Duration::from_secs(7), &dir, &mut pty, &mut rx, &mut tx, &mut status, &mut audit);
    done.store(true, Ordering::Relaxed);
    let (frames, buffer_scale_requests, limits, damage, selected, scrolled, pools) = worker.join().unwrap();
    let mut size: libc::winsize = unsafe { std::mem::zeroed() };
    assert_eq!(unsafe { libc::ioctl(pty.fd(), libc::TIOCGWINSZ, &mut size) }, 0);
    let cleanup = pty.cleanup();
    assert!(cleanup.complete, "{cleanup:?}");
    // The client has destroyed its buffer and closed its descriptor: what
    // the compositor's descriptor still maps is what it would show.
    let pool = pools.last().map_or(Vec::new(), |(fd, size)| crate::shm::tests::mapped(fd.as_fd(), *size as usize));
    let rx = std::fs::read(dir.join("pty-rx.bin")).unwrap();
    let state = live(&dir).map(|(fields, _)| fields).unwrap_or_default();
    std::fs::remove_dir_all(dir).unwrap();
    Seen { frames, buffer_scale_requests, limits, damage, selected, scrolled, winsize: (size.ws_col, size.ws_row), audit, result: result.map_err(|e| e.to_string()), pool, rx, state }
}
/// The frames before and after the scale change: (first, last), and every
/// frame is one of the two (nothing was ever committed at another size).
fn two_states(seen: &Seen) -> (Frame, Frame) {
    let (first, last) = (seen.frames[0], *seen.frames.last().unwrap());
    let change = seen.frames.iter().position(|f| *f == last).unwrap();
    assert!(seen.frames[..change].iter().all(|f| *f == first) && seen.frames[change..].iter().all(|f| *f == last), "{:?}", seen.frames);
    (first, last)
}
#[test]
fn host_real_wayland_fractional_scale_240_then_180_rerasterises_live() {
    // Everything is advertised: the fractional scale wins over the whole ones.
    let seen = run_scale(Case { compositor: 6, fractional: Some(240), viewporter: true, preferred: Some(2), output: Some(2), configure: (0, 0), then: Some(180) });
    let (first, last) = two_states(&seen);
    // Scale 2: 22 x 44 cells, 80 x 24 of them plus 16 px padding; logical 896 x 544.
    assert_eq!(first, Frame { buffer: (1792, 1088), buffer_scale: 1, destination: Some((896, 544)), geometry: Some((0, 0, 896, 544)) });
    // Scale 1.5 in the same 896 x 544: 16 x 33 cells, 82 x 24 of them plus 12 px
    // padding = 1336 x 816; logical ceil(1336 / 1.5) = 891, buffer round(891 x 1.5) = 1337.
    assert_eq!(last, Frame { buffer: (1337, 816), buffer_scale: 1, destination: Some((891, 544)), geometry: Some((0, 0, 891, 544)) });
    assert!(seen.buffer_scale_requests.is_empty(), "buffer scale stays 1 with a viewport: {:?}", seen.buffer_scale_requests);
    assert_eq!(seen.winsize, (82, 24), "the PTY followed the scale change");
    assert_eq!(seen.audit["scale"], serde_json::json!({"scale120":180,"font_px":27.0,"cell":[16,33],"grid":[82,24],"buffer":[1337,816],"logical":[891,544],"buffer_scale":1,"fractional_protocol":true}));
    // Pointer positions in logical units selected the same cells at both scales,
    // and two detents are six lines at scale 1.5.
    assert_eq!((seen.selected, seen.scrolled), ((Some(4), Some(6)), Some(6)));
    assert_eq!(seen.damage.0, 0, "damage is in buffer coordinates only");
    assert!(seen.damage.1 >= seen.frames.len());
    // Minimum size in logical units, again after the change: 20 x 6 cells.
    assert_eq!(seen.limits, [(236, 148), (236, 148), (230, 148)]);
}
#[test]
fn host_real_wayland_fractional_scale_configured_size_150_then_300() {
    let seen = run_scale(Case { compositor: 6, fractional: Some(150), viewporter: true, preferred: None, output: None, configure: (1000, 600), then: Some(300) });
    let (first, last) = two_states(&seen);
    // 1000 x 600 at 1.25: 1250 x 750 device; 14 x 28 cells, 10 px padding: 87 x 26.
    assert_eq!(first, Frame { buffer: (1239, 749), buffer_scale: 1, destination: Some((991, 599)), geometry: Some((0, 0, 991, 599)) });
    // 991 x 599 at 2.5: 2478 x 1498 device; 27 x 55 cells, 20 px padding: 90 x 26.
    assert_eq!(last, Frame { buffer: (2470, 1470), buffer_scale: 1, destination: Some((988, 588)), geometry: Some((0, 0, 988, 588)) });
    for frame in &seen.frames {
        let scale = if *frame == first { 150 } else { 300 };
        let (w, h) = frame.destination.unwrap();
        assert_eq!(frame.buffer, (scale::to_device(w as usize, scale) as i32, scale::to_device(h as usize, scale) as i32));
    }
    assert_eq!(seen.winsize, (90, 26));
    assert_eq!((seen.selected, seen.scrolled), ((Some(4), Some(6)), Some(6)));
    assert_eq!(seen.damage.0, 0);
}
#[test]
fn host_real_wayland_no_scale_protocols_is_unchanged() {
    // The original compositor: wl_compositor 4, no output, no scale protocol.
    let seen = run_scale(Case { compositor: 4, fractional: None, viewporter: false, preferred: None, output: None, configure: (0, 0), then: None });
    assert!(seen.frames.iter().all(|f| *f == Frame { buffer: (896, 544), buffer_scale: 1, destination: None, geometry: Some((0, 0, 896, 544)) }), "{:?}", seen.frames);
    assert!(seen.buffer_scale_requests.is_empty());
    assert_eq!(seen.limits, [(236, 148)]);
    assert_eq!(seen.winsize, (80, 24));
    assert_eq!(seen.audit["scale"]["scale120"], 120);
    assert_eq!((seen.selected.0, seen.scrolled), (Some(4), Some(6)), "the same drag and the same six lines as at any scale");
}
#[test]
fn host_real_wayland_whole_scale_without_fractional_protocol() {
    // wl_surface.preferred_buffer_scale 2 (it wins over the output's 1), then 1.
    let seen = run_scale(Case { compositor: 6, fractional: None, viewporter: true, preferred: Some(2), output: Some(1), configure: (0, 0), then: Some(120) });
    let (first, last) = two_states(&seen);
    assert_eq!(first, Frame { buffer: (1792, 1088), buffer_scale: 2, destination: None, geometry: Some((0, 0, 896, 544)) });
    assert_eq!(last, Frame { buffer: (896, 544), buffer_scale: 1, destination: None, geometry: Some((0, 0, 896, 544)) });
    assert_eq!(seen.buffer_scale_requests, [2, 1]);
    assert_eq!(seen.winsize, (80, 24));
    assert_eq!((seen.selected, seen.scrolled), ((Some(4), Some(6)), Some(6)));
    // Only the output's scale (an older compositor): 1 becomes 3.
    let seen = run_scale(Case { compositor: 4, fractional: None, viewporter: false, preferred: None, output: Some(1), configure: (900, 550), then: Some(360) });
    let (first, last) = two_states(&seen);
    assert_eq!(first, Frame { buffer: (896, 544), buffer_scale: 1, destination: None, geometry: Some((0, 0, 896, 544)) });
    // 896 x 544 at 3: 2688 x 1632 device; 32 x 66 cells, 24 px padding: 82 x 24 = 2672 x 1632.
    assert_eq!(last, Frame { buffer: (2673, 1632), buffer_scale: 3, destination: None, geometry: Some((0, 0, 891, 544)) });
    assert_eq!(seen.winsize, (82, 24));
    assert_eq!((seen.selected, seen.scrolled), ((Some(4), Some(6)), Some(6)));
    // A fractional manager without a viewporter cannot be used: the output's scale applies.
    let seen = run_scale(Case { compositor: 4, fractional: Some(150), viewporter: false, preferred: None, output: Some(2), configure: (0, 0), then: None });
    assert!(seen.frames.iter().all(|f| *f == Frame { buffer: (1792, 1088), buffer_scale: 2, destination: None, geometry: Some((0, 0, 896, 544)) }), "{:?}", seen.frames);
    assert_eq!(seen.audit["scale"]["fractional_protocol"], false);
}

/// Runs `run_scale_any` on the plain compositor with one SHM policy.
fn run_shm(policy: crate::shm::Policy) -> Seen {
    crate::shm::TEST_POLICY.with(|p| p.set(Some(policy)));
    let seen = run_scale_any(Case { compositor: 4, fractional: None, viewporter: false, preferred: None, output: None, configure: (0, 0), then: None });
    crate::shm::TEST_POLICY.with(|p| p.set(None));
    seen
}
#[test]
fn host_real_wayland_pool_is_posix_shared_memory_and_shows_the_same_pixels() {
    let posix = run_shm(crate::shm::Policy::default());
    assert_eq!(posix.result, Ok(()), "{}", posix.audit);
    assert_eq!(posix.audit["stop_cause"], "window_close");
    assert_eq!(posix.audit["shm"], serde_json::json!({"backing":"posix","fallback":null,"posix_buffers":1,"file_buffers":0}));
    assert_eq!(posix.state.get("shm").map(String::as_str), Some("posix"), "{:?}", posix.state);
    assert_eq!(posix.state.get("buffer").map(String::as_str), Some("896x544"));
    // The compositor's mapping of the descriptor it was sent shows what the
    // renderer draws for the same PTY output, scrolled back the same six lines.
    let (w, h) = (896usize, 544usize);
    let mut term = term::Term::new(80, 24).unwrap();
    term.feed(&posix.rx);
    assert!(term.scroll_view(posix.scrolled.unwrap() as isize));
    let mut renderer = render::Renderer::new().unwrap();
    renderer.set_metrics(scale::Metrics::new(scale::BASE_FONT_PX, scale::UNIT));
    let mut expected = vec![0u8; w * h * 4];
    assert!(renderer.draw(&term, &mut expected, w, h).is_some());
    assert_eq!(posix.pool.len(), expected.len());
    assert!(posix.pool == expected, "the pool differs from the rendered screen");
    let colours: std::collections::HashSet<&[u8]> = posix.pool.chunks(4).collect();
    assert!(colours.len() > 2, "text was drawn, not one flat colour: {}", colours.len());
    // The earlier backing, asked for as the environment override does: the same pixels.
    let file = run_shm(crate::shm::Policy::from_env(Some("file")));
    assert_eq!(file.result, Ok(()), "{}", file.audit);
    assert_eq!(file.audit["shm"], serde_json::json!({"backing":"file","fallback":null,"posix_buffers":0,"file_buffers":1}));
    assert_eq!(file.state.get("shm").map(String::as_str), Some("file"));
    assert!(file.pool == posix.pool, "file and POSIX backings show different pixels");
    // (How many frames were committed depends on timing; what they were does not.)
    assert_eq!((file.frames.last(), file.selected, file.scrolled, file.winsize), (posix.frames.last(), posix.selected, posix.scrolled, posix.winsize));
    // Nothing of this process is left in /dev/shm.
    assert_eq!(crate::shm::tests::leftover_names(), Vec::<String>::new());
}
#[test]
fn host_real_wayland_falls_back_to_the_file_when_shared_memory_fails() {
    let seen = run_shm(crate::shm::Policy { fail_posix: Some("ftruncate"), ..crate::shm::Policy::default() });
    assert_eq!(seen.result, Ok(()), "{}", seen.audit);
    assert_eq!(seen.audit["ready"], true);
    assert_eq!(seen.audit["stop_cause"], "window_close");
    assert_eq!(seen.audit["shm"], serde_json::json!({"backing":"file","fallback":"shm buffer 896x544 (1949696 bytes): posix ftruncate: errno 28","posix_buffers":0,"file_buffers":1}));
    assert_eq!(seen.state.get("shm").map(String::as_str), Some("file"));
    assert_eq!((seen.selected.0, seen.scrolled, seen.winsize), (Some(4), Some(6), (80, 24)));
    assert_eq!(seen.pool.len(), 896 * 544 * 4);
    assert!(seen.pool.chunks(4).collect::<std::collections::HashSet<_>>().len() > 2);
    assert_eq!(crate::shm::tests::leftover_names(), Vec::<String>::new());
}
#[test]
fn host_real_wayland_buffer_allocation_failure_ends_the_session_with_its_cause() {
    let seen = run_shm(crate::shm::Policy { fail_posix: Some("ftruncate"), fail_file: Some("set_len"), ..crate::shm::Policy::default() });
    // The text `run` puts into result.json's `errors` and prints on stderr.
    assert_eq!(seen.result, Err("shm buffer 896x544 (1949696 bytes): file set_len: errno 28 (after posix ftruncate: errno 28)".to_string()));
    assert_eq!(seen.audit["stop_cause"], "fault");
    assert_eq!(seen.audit["ready"], false);
    assert_eq!(seen.audit["shm"], serde_json::json!({"backing":null,"fallback":null,"posix_buffers":0,"file_buffers":0}));
    assert!(seen.frames.is_empty() && seen.pool.is_empty(), "no pool was created and nothing was committed");
}
