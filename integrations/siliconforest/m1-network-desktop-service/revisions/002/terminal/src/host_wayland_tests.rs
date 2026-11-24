//! Real host Wayland server/client and PTYs; all input below is synthetic.
use super::*;
use std::{
    os::unix::fs::DirBuilderExt,
    sync::{atomic::AtomicBool, Arc},
};
use wayland_protocols::xdg::{
    decoration::zv1::server::{
        zxdg_decoration_manager_v1 as dm, zxdg_toplevel_decoration_v1 as dec,
    },
    shell::server::{xdg_surface as xs, xdg_toplevel as xt, xdg_wm_base as wm},
};
use wayland_server::protocol::{
    wl_buffer as sb, wl_callback as cb, wl_compositor as sc, wl_keyboard as kb, wl_region as sr,
    wl_seat as seat, wl_shm as shm, wl_shm_pool as pool, wl_surface as ss,
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
impl GlobalDispatch<seat::WlSeat, ()> for Server {
    fn bind(
        _: &mut Self,
        _: &DisplayHandle,
        _: &Client,
        r: New<seat::WlSeat>,
        _: &(),
        init: &mut DataInit<'_, Self>,
    ) {
        let seat = init.init(r, ());
        seat.capabilities(seat::Capability::Keyboard);
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
            ss::Request::Commit => {
                if !s.configured && s.xdg.is_some() {
                    s.configured = true;
                    s.top.as_ref().unwrap().configure(0, 0, vec![]);
                    s.xdg.as_ref().unwrap().configure(1);
                    if s.mode == Mode::CloseBeforeReady {
                        s.top.as_ref().unwrap().close();
                        s.closed = true;
                    }
                }
                if let Some(buffer) = s.buffer.take() {
                    s.commits += 1;
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
        _: &mut Self,
        _: &Client,
        _: &shm::WlShm,
        r: shm::Request,
        _: &(),
        _: &DisplayHandle,
        i: &mut DataInit<'_, Self>,
    ) {
        if let shm::Request::CreatePool { id, .. } = r {
            i.init(id, ());
        }
    }
}
impl wayland_server::Dispatch<pool::WlShmPool, ()> for Server {
    fn request(
        _: &mut Self,
        _: &Client,
        _: &pool::WlShmPool,
        r: pool::Request,
        _: &(),
        _: &DisplayHandle,
        i: &mut DataInit<'_, Self>,
    ) {
        if let pool::Request::CreateBuffer { id, .. } = r {
            i.init(id, ());
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
ignore!(xt::XdgToplevel);
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
        None,
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
