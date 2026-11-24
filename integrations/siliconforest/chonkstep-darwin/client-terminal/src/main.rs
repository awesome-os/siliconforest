//! Finite native Darwin PTY terminal; the shell is /bin/sh, never a local REPL.
mod grid;
mod keys;
mod negotiation;
mod protocol;
mod pty;
mod render;
use protocol::{FrameState, Proof, MAX_RX, MAX_TX};
use std::{
    collections::{BTreeSet, VecDeque},
    fs::{File, OpenOptions},
    io::{self, Read, Seek, SeekFrom, Write},
    os::{
        fd::{AsFd, AsRawFd, FromRawFd},
        unix::{
            ffi::OsStrExt,
            fs::{DirBuilderExt, MetadataExt, OpenOptionsExt},
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
        wl_buffer, wl_callback, wl_compositor, wl_keyboard, wl_registry, wl_seat, wl_shm,
        wl_shm_pool, wl_surface,
    },
    Connection, Dispatch, QueueHandle, WEnum,
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
fn announce(line: String) -> io::Result<()> {
    let mut out = io::stdout().lock();
    writeln!(out, "{line}")?;
    out.flush()
}
struct State {
    compositor: Option<wl_compositor::WlCompositor>,
    shm: Option<wl_shm::WlShm>,
    shell: Option<xdg_wm_base::XdgWmBase>,
    decoration: Option<zxdg_decoration_manager_v1::ZxdgDecorationManagerV1>,
    seat: Option<wl_seat::WlSeat>,
    keyboard: Option<wl_keyboard::WlKeyboard>,
    own: Option<wl_surface::WlSurface>,
    registry_done: bool,
    closed: bool,
    error: Option<String>,
    ssd: negotiation::Negotiation,
    keys: keys::Keys,
    held: BTreeSet<u32>,
    neutral: bool,
    frames: FrameState,
    proof: Proof,
    tx: VecDeque<u8>,
    tx_count: usize,
    pending_size: Option<(usize, usize)>,
    size: (usize, usize),
    dirty: bool,
}
impl State {
    fn new(nonce: String) -> Self {
        Self {
            compositor: None,
            shm: None,
            shell: None,
            decoration: None,
            seat: None,
            keyboard: None,
            own: None,
            registry_done: false,
            closed: false,
            error: None,
            ssd: Default::default(),
            keys: keys::Keys::new(),
            held: BTreeSet::new(),
            neutral: false,
            frames: FrameState::new(),
            proof: Proof::new(nonce),
            tx: VecDeque::new(),
            tx_count: 0,
            pending_size: None,
            size: (80 * render::CW + 16, 24 * render::CH + 16),
            dirty: true,
        }
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
        if let wl_registry::Event::Global {
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
                        s.compositor = Some(r.bind(name, 4, q, ()));
                    }
                }
                "wl_shm" => s.shm = Some(r.bind(name, 1, q, ())),
                "xdg_wm_base" => s.shell = Some(r.bind(name, version.min(6), q, ())),
                "zxdg_decoration_manager_v1" => s.decoration = Some(r.bind(name, 1, q, ())),
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
                    s.keys.keymap(&b)?;
                    Ok(())
                })();
                if let Err(e) = result {
                    s.reject(e);
                }
            }
            wl_keyboard::Event::Enter { surface, keys, .. } => {
                if s.own.as_ref() != Some(&surface) || keys.len() % 4 != 0 || keys.len() > 128 {
                    s.reject("invalid focus/held-key event");
                    return;
                }
                s.held = keys
                    .as_chunks::<4>()
                    .0
                    .iter()
                    .map(|b| u32::from_ne_bytes(*b))
                    .collect();
                s.keys.focused = true;
            }
            wl_keyboard::Event::Leave { .. } => {
                s.keys.focused = false;
                s.held.clear();
                if !s.tx.is_empty() {
                    s.reject("focus left with queued input");
                }
            }
            wl_keyboard::Event::Modifiers {
                mods_depressed,
                mods_latched,
                mods_locked,
                group,
                ..
            } => {
                s.keys
                    .modifiers(mods_depressed, mods_latched, mods_locked, group);
                s.neutral = (mods_depressed | mods_latched | mods_locked | group) == 0;
            }
            wl_keyboard::Event::Key { key, state, .. } => {
                let down = match state {
                    WEnum::Value(wl_keyboard::KeyState::Pressed) => true,
                    WEnum::Value(wl_keyboard::KeyState::Released) => false,
                    _ => {
                        s.reject("invalid key state");
                        return;
                    }
                };
                if down {
                    if !s.held.insert(key) {
                        s.reject("duplicate key press");
                        return;
                    }
                } else {
                    s.held.remove(&key);
                }
                match s.keys.press(key, down) {
                    Ok(bytes) => {
                        if !bytes.is_empty() && !s.proof.ready {
                            s.reject("text input before READY");
                            return;
                        }
                        if s.tx_count + s.tx.len() + bytes.len() > MAX_TX {
                            s.reject("input quota exceeded");
                            return;
                        }
                        s.tx.extend(bytes);
                    }
                    Err(e) => s.reject(e),
                }
            }
            _ => (),
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
            s.ssd.surface_configured(serial);
            if let Some(size) = s.pending_size.take() {
                s.size = size;
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
                let w = if width == 0 { s.size.0 } else { width as usize };
                let h = if height == 0 {
                    s.size.1
                } else {
                    height as usize
                };
                if !(20 * render::CW + 16..=160 * render::CW + 16).contains(&w)
                    || !(6 * render::CH + 16..=60 * render::CH + 16).contains(&h)
                {
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
delegate_noop!(State: ignore wl_compositor::WlCompositor);
delegate_noop!(State: ignore wl_surface::WlSurface);
delegate_noop!(State: ignore wl_shm::WlShm);
delegate_noop!(State: ignore wl_shm_pool::WlShmPool);
delegate_noop!(State: ignore zxdg_decoration_manager_v1::ZxdgDecorationManagerV1);

struct Buffer {
    file: File,
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
    ) -> Result<Self> {
        let bytes = size
            .0
            .checked_mul(size.1)
            .and_then(|n| n.checked_mul(4))
            .ok_or("buffer overflow")?;
        if bytes > 32 * 1024 * 1024 {
            return Err("buffer exceeds bound".into());
        }
        let path = dir.join(format!("shm-{seq}"));
        let f = OpenOptions::new()
            .read(true)
            .write(true)
            .create_new(true)
            .mode(0o600)
            .open(&path)?;
        std::fs::remove_file(path)?;
        f.set_len(bytes as u64)?;
        let pool = shm.create_pool(f.as_fd(), bytes as i32, q, ());
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
            file: f,
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
        top.set_title("Chonkstep — Darwin PTY terminal (host-origin qualification)".into());
        top.set_app_id("org.darwinlab.terminal".into());
        top.set_min_size((20 * render::CW + 16) as i32, (6 * render::CH + 16) as i32);
        top.set_max_size(
            (160 * render::CW + 16) as i32,
            (60 * render::CH + 16) as i32,
        );
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
        })
    }
}
impl Drop for Window {
    fn drop(&mut self) {
        self.dec.destroy();
        self.top.destroy();
        self.xdg.destroy();
        self.surface.destroy();
    }
}
fn connect(path: &Path) -> io::Result<UnixStream> {
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
        let end = Instant::now() + Duration::from_secs(3);
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
    OpenOptions::new()
        .write(true)
        .create_new(true)
        .mode(0o600)
        .open(dir.join(name))
}
fn private_dir(socket: &Path, nonce: &str) -> io::Result<PathBuf> {
    let parent = socket
        .parent()
        .ok_or_else(|| io::Error::other("socket needs parent"))?;
    let m = std::fs::symlink_metadata(parent)?;
    if !m.is_dir() || m.uid() != unsafe { libc::geteuid() } || m.mode() & 0o077 != 0 {
        return Err(io::Error::other(
            "socket parent must be owned private 0700 directory",
        ));
    }
    let dir = parent.join(format!("terminal-{nonce}"));
    std::fs::DirBuilder::new().mode(0o700).create(&dir)?;
    Ok(dir)
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

fn session(
    stream: UnixStream,
    nonce: &str,
    seconds: u64,
    dir: &Path,
    pty: &mut pty::Pty,
    rx: &mut File,
    tx: &mut File,
) -> Result<serde_json::Value> {
    let conn = Connection::from_socket(stream)?;
    let mut queue = conn.new_event_queue::<State>();
    let q = queue.handle();
    conn.display().get_registry(&q, ());
    conn.display().sync(&q, 0u8);
    let mut s = State::new(nonce.to_owned());
    let mut window = None;
    let mut buffer: Option<Buffer> = None;
    let mut seq = 0;
    let mut grid = grid::Grid::new(80, 24)?;
    let renderer = render::Renderer::new()?;
    let started = Instant::now();
    let mut received = 0;
    let mut checked_child_recorded = false;
    while started.elapsed() < Duration::from_secs(seconds)
        && !STOP.load(Ordering::Relaxed)
        && !s.closed
    {
        pty.setup()?;
        if pty.exec_checked() && !checked_child_recorded {
            let mut child = log(dir, "child-ready.json")?;
            serde_json::to_writer_pretty(
                &mut child,
                &serde_json::json!({"parent_pid":std::process::id(),"child_pid":pty.pid(),"sid":pty.pid(),"pgid":pty.pid(),"session_and_ctty_checked_in_child":true,"exec_status_pipe_closed":true,"shell":"/bin/sh"}),
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
        if !s.proof.ready && started.elapsed() > Duration::from_secs(20) {
            return Err("PTY/keymap/focus/SSD/frame readiness deadline".into());
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
                    s.dirty = true;
                    if s.proof.receive(&b[..n])? {
                        rx.sync_data()?;
                        announce(format!("DL11_TERMINAL_RESPONSE {nonce} origin=pty_shell system=Darwin machine=arm64 release=25.5.0"))?;
                    }
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
        if s.frames.can_draw(s.ssd.ready()) && s.dirty {
            if buffer.as_ref().is_none_or(|b| b.size != s.size) {
                buffer.take();
                seq += 1;
                buffer = Some(Buffer::new(
                    s.shm.as_ref().ok_or("no shm")?,
                    &q,
                    dir,
                    s.size,
                    seq,
                )?);
                let cols = (s.size.0 - 16) / render::CW;
                let rows = (s.size.1 - 16) / render::CH;
                grid.resize(cols, rows)?;
                pty.resize(cols as u16, rows as u16)?;
            }
            let b = buffer.as_mut().unwrap();
            let source = renderer.pixels(&grid);
            let sw = grid.cols * render::CW + 16;
            let sh = grid.rows * render::CH + 16;
            let mut pixels = vec![0u8; s.size.0 * s.size.1 * 4];
            for p in pixels.as_chunks_mut::<4>().0 {
                p.copy_from_slice(&[26, 22, 20, 255]);
            }
            for y in 0..sh {
                pixels[y * s.size.0 * 4..y * s.size.0 * 4 + sw * 4]
                    .copy_from_slice(&source[y * sw * 4..(y + 1) * sw * 4]);
            }
            b.file.seek(SeekFrom::Start(0))?;
            b.file.write_all(&pixels)?;
            let w = window.as_ref().ok_or("no configured window")?;
            w.xdg
                .set_window_geometry(0, 0, s.size.0 as i32, s.size.1 as i32);
            s.frames.commit(s.ssd.ready(), received)?;
            w.surface.frame(&q, 1u8);
            w.surface.attach(Some(&b.buffer), 0, 0);
            w.surface
                .damage_buffer(0, 0, s.size.0 as i32, s.size.1 as i32);
            w.surface.commit();
            s.dirty = false;
        }
        if s.proof.arm(
            pty.ready(),
            s.keys.ready(),
            s.keys.focused && s.held.is_empty() && s.neutral,
            s.ssd.ready(),
            if s.frames.visible_rx > 0 {
                s.frames.callbacks
            } else {
                0
            },
        ) {
            announce(format!("DL11_TERMINAL_READY {nonce} origin=host_sfserialhid pty=ready keymap=xkb_v1 focus=own_surface frame=ready"))?;
        }
        if !s.tx.is_empty() {
            let bytes: Vec<u8> = s.tx.iter().copied().take(512).collect();
            match pty.write(&bytes) {
                Ok(0) => return Err("zero PTY write".into()),
                Ok(n) => {
                    tx.write_all(&bytes[..n])?;
                    s.tx.drain(..n);
                    s.tx_count += n;
                }
                Err(e)
                    if matches!(
                        e.kind(),
                        io::ErrorKind::WouldBlock | io::ErrorKind::Interrupted
                    ) => {}
                Err(e) => return Err(e.into()),
            }
        }
        if let Some(guard) = queue.prepare_read() {
            let writable = flush(&conn)?;
            let mut fds = [
                libc::pollfd {
                    fd: conn.as_fd().as_raw_fd(),
                    events: libc::POLLIN | if writable { libc::POLLOUT } else { 0 },
                    revents: 0,
                },
                libc::pollfd {
                    fd: pty.fd(),
                    events: libc::POLLIN | if s.tx.is_empty() { 0 } else { libc::POLLOUT },
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
    // Destroy decoration before toplevel; server keeps any attached SHM fd alive.
    window.take();
    buffer.take();
    let _ = flush(&conn);
    let mut final_grid = log(dir, "grid-final.txt")?;
    final_grid.write_all(grid.text().as_bytes())?;
    final_grid.sync_all()?;
    Ok(
        serde_json::json!({"configure_serial":s.ssd.serial(),"ready":s.proof.ready,"response":s.proof.response,"frames":s.frames.callbacks,"commits":s.frames.commits,"pty_rx_bytes":received,"pty_tx_bytes":s.tx_count,"unsupported_terminal_sequences":grid.unsupported,"term_signal":STOP.load(Ordering::Relaxed),"window_closed":s.closed,"pty_eof":pty.eof}),
    )
}
fn run() -> Result<()> {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.len() != 6 || args[0] != "--socket" || args[2] != "--nonce" || args[4] != "--seconds" {
        return Err("expected --socket PATH --nonce HEX48 --seconds N".into());
    }
    let socket = Path::new(&args[1]);
    let nonce = &args[3];
    let seconds: u64 = args[5].parse()?;
    if !socket.is_absolute()
        || nonce.len() != 48
        || !nonce.bytes().all(|b| b.is_ascii_hexdigit())
        || !(1..=120).contains(&seconds)
    {
        return Err("invalid path, nonce, or finite duration".into());
    }
    if !cfg!(all(target_os = "macos", target_arch = "aarch64")) {
        return Err(
            "production terminal requires aarch64-apple-darwin; host tests do not qualify Darwin"
                .into(),
        );
    }
    signals()?;
    let dir = private_dir(socket, nonce)?;
    let mut rx = log(&dir, "pty-rx.bin")?;
    let mut tx = log(&dir, "pty-tx.bin")?;
    let mut pty = pty::Pty::shell(80, 24)?;
    let pid = pty.pid();
    let mut child = log(&dir, "child.json")?;
    serde_json::to_writer_pretty(
        &mut child,
        &serde_json::json!({"parent_pid":std::process::id(),"child_pid":pid,"setup_checked":false,"qualification_only_no_detached_jobs":true}),
    )?;
    child.write_all(b"\n")?;
    child.sync_all()?;
    drop(child);
    let result = match connect(socket) {
        Ok(stream) => session(stream, nonce, seconds, &dir, &mut pty, &mut rx, &mut tx),
        Err(error) => Err(error.into()),
    };
    // Always reap before returning errors, even on protocol disconnect/window close/TERM.
    let cleanup = pty.cleanup();
    rx.sync_all()?;
    tx.sync_all()?;
    let mut record = serde_json::json!({"schema":1,"nonce":nonce,"origin":"host_sfserialhid","shell":"/bin/sh","term":"dumb","child_pid":pid,"cleanup":{"complete":cleanup.complete,"status":cleanup.status,"term_sent":cleanup.term_sent,"kill_sent":cleanup.kill_sent,"error":cleanup.error},"darwin_runtime_proof_requires_external_capture":true});
    match &result {
        Ok(v) => record["session"] = v.clone(),
        Err(e) => record["error"] = serde_json::Value::String(e.to_string()),
    }
    let mut out = log(&dir, "result.json")?;
    serde_json::to_writer_pretty(&mut out, &record)?;
    out.write_all(b"\n")?;
    out.sync_all()?;
    let observed = result?;
    if !cleanup.complete {
        return Err("child cleanup incomplete; outer supervisor required".into());
    }
    if observed["response"] != true {
        return Err("strict PTY shell response not observed".into());
    }
    announce(format!("DL11_TERMINAL_DONE {nonce} cleanup=reaped"))?;
    Ok(())
}
fn main() {
    if let Err(e) = run() {
        eprintln!("darwin-wayland-terminal: {e}");
        std::process::exit(1);
    }
}

#[cfg(test)]
mod lifecycle_tests {
    use super::*;
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
            assert!(session(stream, &"0".repeat(48), 1, &path, &mut p, &mut rx, &mut tx).is_err());
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
