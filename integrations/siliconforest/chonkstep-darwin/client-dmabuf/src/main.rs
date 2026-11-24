//! A real Wayland client that submits GPU buffers through linux-dmabuf-v1.
//!
//! It allocates three linear XRGB8888 buffers on the asahi render node (the
//! same GEM ioctl the compositor uses for its scanout buffers), exports each
//! as a dma-buf, hands the fds to the compositor with `create_immed`, and then
//! animates: every frame callback it fills a buffer the compositor has
//! released (through a CPU mapping; there is no GL here) and commits it.
//! The compositor never copies the pixels, so what is on screen is this
//! process's GPU memory. Lines: DMACLIENT.
use std::{
    ffi::{c_char, c_int, c_void, CStr, CString},
    os::fd::{AsFd, BorrowedFd},
    os::unix::net::UnixStream,
    time::{Duration, Instant},
};
use wayland_client::{
    delegate_noop,
    protocol::{wl_buffer, wl_callback, wl_compositor, wl_registry, wl_surface},
    Connection, Dispatch, QueueHandle,
};
use wayland_protocols::wp::linux_dmabuf::zv1::client::{zwp_linux_buffer_params_v1, zwp_linux_dmabuf_v1};
use wayland_protocols::xdg::shell::client::{xdg_surface, xdg_toplevel, xdg_wm_base};

const W: usize = 640;
const H: usize = 400;
const XR24: u32 = 0x3432_5258;
/// The block in the middle of every frame; the compositor's WMEXT line reads it back.
const MARK: [u8; 3] = [0x33, 0x99, 0xcc];

// libdrm, opened at run time like the compositor does (xf86drm.h, libdrm 2.4.133).
#[repr(C)]
struct DrmVersion {
    major: c_int,
    minor: c_int,
    patch: c_int,
    name_len: c_int,
    name: *mut c_char,
    date_len: c_int,
    date: *mut c_char,
    desc_len: c_int,
    desc: *mut c_char,
}
struct Drm {
    get_version: unsafe extern "C" fn(c_int) -> *mut DrmVersion,
    free_version: unsafe extern "C" fn(*mut DrmVersion),
    ioctl: unsafe extern "C" fn(c_int, libc::c_ulong, *mut c_void) -> c_int,
    prime_handle_to_fd: unsafe extern "C" fn(c_int, u32, u32, *mut c_int) -> c_int,
}
impl Drm {
    fn load() -> Result<Self, String> {
        let default = if cfg!(target_os = "macos") { "/usr/local/lib/mesa-asahi/libdrm.2.dylib" } else { "libdrm.so.2" };
        let path = std::env::var("DMACLIENT_LIBDRM").unwrap_or_else(|_| default.into());
        let c = CString::new(path.clone()).map_err(|_| "library path")?;
        let handle = unsafe { libc::dlopen(c.as_ptr(), libc::RTLD_NOW | libc::RTLD_GLOBAL) };
        if handle.is_null() {
            return Err(format!("dlopen {path} failed"));
        }
        let sym = |name: &CStr| -> Result<*mut c_void, String> {
            let p = unsafe { libc::dlsym(handle, name.as_ptr()) };
            if p.is_null() { Err(format!("missing symbol {name:?}")) } else { Ok(p) }
        };
        unsafe {
            Ok(Self {
                get_version: std::mem::transmute::<*mut c_void, unsafe extern "C" fn(c_int) -> *mut DrmVersion>(sym(c"drmGetVersion")?),
                free_version: std::mem::transmute::<*mut c_void, unsafe extern "C" fn(*mut DrmVersion)>(sym(c"drmFreeVersion")?),
                ioctl: std::mem::transmute::<*mut c_void, unsafe extern "C" fn(c_int, libc::c_ulong, *mut c_void) -> c_int>(sym(c"drmIoctl")?),
                prime_handle_to_fd: std::mem::transmute::<*mut c_void, unsafe extern "C" fn(c_int, u32, u32, *mut c_int) -> c_int>(sym(c"drmPrimeHandleToFD")?),
            })
        }
    }
    /// `/dev/dri/renderD*` whose driver is `asahi`.
    fn open_render_node(&self) -> Result<c_int, String> {
        let mut seen = Vec::new();
        for entry in std::fs::read_dir("/dev/dri").map_err(|e| format!("/dev/dri: {e}"))?.flatten() {
            let name = entry.file_name().to_string_lossy().into_owned();
            if !name.starts_with("renderD") {
                continue;
            }
            let path = CString::new(format!("/dev/dri/{name}")).unwrap();
            let fd = unsafe { libc::open(path.as_ptr(), libc::O_RDWR | libc::O_CLOEXEC) };
            if fd < 0 {
                continue;
            }
            let v = unsafe { (self.get_version)(fd) };
            let driver = if v.is_null() || unsafe { (*v).name.is_null() } {
                String::from("?")
            } else {
                unsafe { CStr::from_ptr((*v).name) }.to_string_lossy().into_owned()
            };
            if !v.is_null() {
                unsafe { (self.free_version)(v) };
            }
            if driver == "asahi" {
                return Ok(fd);
            }
            seen.push(format!("{name}:{driver}"));
            unsafe { libc::close(fd) };
        }
        Err(format!("no asahi render node ({})", seen.join(",")))
    }
}

fn os_error(call: &str) -> String {
    let e = std::io::Error::last_os_error();
    format!("{call}: errno {} ({e})", e.raw_os_error().unwrap_or(0))
}

#[repr(C)]
#[derive(Default)]
struct GemCreate {
    size: u64,
    flags: u32,
    vm_id: u32,
    handle: u32,
    pad: u32,
}
#[repr(C)]
#[derive(Default)]
struct GemMmapOffset {
    handle: u32,
    flags: u32,
    offset: u64,
}
struct GpuBuffer {
    map: *mut u8,
    size: usize,
    fd: c_int,
}
impl GpuBuffer {
    /// The compositor's own sequence: GEM_CREATE, MMAP_OFFSET, mmap, PRIME export.
    fn create(drm: &Drm, render: c_int) -> Result<Self, String> {
        let size = (W * H * 4 + 0x3fff) & !0x3fff;
        unsafe {
            let mut create = GemCreate { size: size as u64, ..Default::default() };
            if (drm.ioctl)(render, 0xC018_6445, &mut create as *mut _ as *mut c_void) != 0 {
                return Err(os_error("DRM_IOCTL_ASAHI_GEM_CREATE"));
            }
            let mut offset = GemMmapOffset { handle: create.handle, ..Default::default() };
            if (drm.ioctl)(render, 0xC010_6446, &mut offset as *mut _ as *mut c_void) != 0 {
                return Err(os_error("DRM_IOCTL_ASAHI_GEM_MMAP_OFFSET"));
            }
            let map = libc::mmap(std::ptr::null_mut(), size, libc::PROT_READ | libc::PROT_WRITE, libc::MAP_SHARED, render, offset.offset as libc::off_t);
            if map == libc::MAP_FAILED {
                return Err(os_error("mmap of the GPU buffer"));
            }
            let mut fd = -1;
            // DRM_CLOEXEC | DRM_RDWR
            if (drm.prime_handle_to_fd)(render, create.handle, 0x0100_0000 | 2, &mut fd) != 0 {
                return Err(os_error("drmPrimeHandleToFD"));
            }
            Ok(Self { map: map as *mut u8, size, fd })
        }
    }
    /// Little-endian XRGB8888: B, G, R, x per pixel; row 0 is the top row.
    fn paint(&mut self, frame: u32) {
        let pixels = unsafe { std::slice::from_raw_parts_mut(self.map, self.size) };
        let shade = (frame % 120) as u8;
        for y in 0..H {
            let row = &mut pixels[y * W * 4..(y + 1) * W * 4];
            for (x, p) in row.chunks_exact_mut(4).enumerate() {
                p.copy_from_slice(&[40 + shade, (x * 255 / W) as u8, (y * 255 / H) as u8, 255]);
            }
        }
        let mut rect = |x0: usize, y0: usize, w: usize, h: usize, rgb: [u8; 3]| {
            for y in y0..(y0 + h).min(H) {
                for x in x0..(x0 + w).min(W) {
                    pixels[(y * W + x) * 4..][..4].copy_from_slice(&[rgb[2], rgb[1], rgb[0], 255]);
                }
            }
        };
        // A bar that moves 8 px per frame, and the fixed block in the middle.
        rect((frame as usize * 8) % (W - 40), 20, 40, 60, [250, 240, 60]);
        rect(W / 2 - 32, H / 2 - 32, 64, 64, MARK);
    }
}

#[derive(Default)]
struct State {
    compositor: Option<wl_compositor::WlCompositor>,
    shell: Option<xdg_wm_base::XdgWmBase>,
    dmabuf: Option<zwp_linux_dmabuf_v1::ZwpLinuxDmabufV1>,
    modifiers: Vec<(u32, u64)>,
    configured: bool,
    closed: bool,
    callbacks: u32,
    callback_pending: bool,
    /// Per buffer: the compositor holds it (committed and not yet released).
    busy: [bool; 3],
    releases: u32,
}
impl Dispatch<wl_registry::WlRegistry, ()> for State {
    fn event(s: &mut Self, r: &wl_registry::WlRegistry, e: wl_registry::Event, _: &(), _: &Connection, q: &QueueHandle<Self>) {
        if let wl_registry::Event::Global { name, interface, version } = e {
            match interface.as_str() {
                "wl_compositor" => s.compositor = Some(r.bind(name, version.min(4), q, ())),
                "xdg_wm_base" => s.shell = Some(r.bind(name, 1, q, ())),
                "zwp_linux_dmabuf_v1" => s.dmabuf = Some(r.bind(name, version.min(3), q, ())),
                _ => {}
            }
        }
    }
}
impl Dispatch<zwp_linux_dmabuf_v1::ZwpLinuxDmabufV1, ()> for State {
    fn event(s: &mut Self, _: &zwp_linux_dmabuf_v1::ZwpLinuxDmabufV1, e: zwp_linux_dmabuf_v1::Event, _: &(), _: &Connection, _: &QueueHandle<Self>) {
        if let zwp_linux_dmabuf_v1::Event::Modifier { format, modifier_hi, modifier_lo } = e {
            s.modifiers.push((format, (u64::from(modifier_hi) << 32) | u64::from(modifier_lo)));
        }
    }
}
impl Dispatch<wl_buffer::WlBuffer, usize> for State {
    fn event(s: &mut Self, _: &wl_buffer::WlBuffer, e: wl_buffer::Event, index: &usize, _: &Connection, _: &QueueHandle<Self>) {
        if let wl_buffer::Event::Release = e {
            s.busy[*index] = false;
            s.releases += 1;
        }
    }
}
impl Dispatch<wl_callback::WlCallback, ()> for State {
    fn event(s: &mut Self, _: &wl_callback::WlCallback, e: wl_callback::Event, _: &(), _: &Connection, _: &QueueHandle<Self>) {
        if let wl_callback::Event::Done { .. } = e {
            s.callbacks += 1;
            s.callback_pending = false;
        }
    }
}
impl Dispatch<xdg_wm_base::XdgWmBase, ()> for State {
    fn event(_: &mut Self, p: &xdg_wm_base::XdgWmBase, e: xdg_wm_base::Event, _: &(), _: &Connection, _: &QueueHandle<Self>) {
        if let xdg_wm_base::Event::Ping { serial } = e {
            p.pong(serial);
        }
    }
}
impl Dispatch<xdg_surface::XdgSurface, ()> for State {
    fn event(s: &mut Self, x: &xdg_surface::XdgSurface, e: xdg_surface::Event, _: &(), _: &Connection, _: &QueueHandle<Self>) {
        if let xdg_surface::Event::Configure { serial } = e {
            x.ack_configure(serial);
            s.configured = true;
        }
    }
}
impl Dispatch<xdg_toplevel::XdgToplevel, ()> for State {
    fn event(s: &mut Self, _: &xdg_toplevel::XdgToplevel, e: xdg_toplevel::Event, _: &(), _: &Connection, _: &QueueHandle<Self>) {
        if let xdg_toplevel::Event::Close = e {
            s.closed = true;
        }
    }
}
delegate_noop!(State: ignore wl_compositor::WlCompositor);
delegate_noop!(State: ignore wl_surface::WlSurface);
delegate_noop!(State: ignore zwp_linux_buffer_params_v1::ZwpLinuxBufferParamsV1);

fn run() -> Result<(), String> {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.len() != 4 || args[0] != "--socket" || args[2] != "--seconds" {
        return Err("expected --socket PATH --seconds N".into());
    }
    let seconds: u64 = args[3].parse().ok().filter(|s| (1..=120).contains(s)).ok_or("seconds must be 1..120")?;
    let err = |e: &dyn std::fmt::Display| e.to_string();

    let drm = Drm::load()?;
    let render = drm.open_render_node()?;
    let mut buffers = Vec::new();
    for _ in 0..3 {
        buffers.push(GpuBuffer::create(&drm, render)?);
    }
    println!("DMACLIENT buffers=3 size={W}x{H} format=XR24 modifier=linear stride={} bytes_each={}", W * 4, buffers[0].size);

    let conn = Connection::from_socket(UnixStream::connect(&args[1]).map_err(|e| err(&e))?).map_err(|e| err(&e))?;
    let mut queue = conn.new_event_queue::<State>();
    let q = queue.handle();
    conn.display().get_registry(&q, ());
    let mut s = State::default();
    queue.roundtrip(&mut s).map_err(|e| err(&e))?;
    let Some(dmabuf) = s.dmabuf.clone() else {
        println!("DMACLIENT result=no-linux-dmabuf-global (the compositor is not on its GL renderer)");
        return Err("zwp_linux_dmabuf_v1 is not advertised".into());
    };
    queue.roundtrip(&mut s).map_err(|e| err(&e))?;
    let list: Vec<String> = s.modifiers.iter().map(|(f, m)| format!("{}:{m:#x}", String::from_utf8_lossy(&f.to_le_bytes()))).collect();
    println!("DMACLIENT advertised {}", list.join(" "));
    if !s.modifiers.contains(&(XR24, 0)) {
        return Err("XR24 linear is not advertised".into());
    }
    let surface = s.compositor.as_ref().ok_or("no wl_compositor")?.create_surface(&q, ());
    let xdg = s.shell.as_ref().ok_or("no xdg_wm_base")?.get_xdg_surface(&surface, &q, ());
    let top = xdg.get_toplevel(&q, ());
    top.set_title("GPU buffers over linux-dmabuf".into());
    top.set_app_id("org.darwinlab.dmabuf-test".into());
    surface.commit();
    let wl_buffers: Vec<wl_buffer::WlBuffer> = buffers
        .iter()
        .enumerate()
        .map(|(index, b)| {
            let params = dmabuf.create_params(&q, ());
            params.add(unsafe { BorrowedFd::borrow_raw(b.fd) }, 0, 0, (W * 4) as u32, 0, 0);
            let buffer = params.create_immed(W as i32, H as i32, XR24, zwp_linux_buffer_params_v1::Flags::empty(), &q, index);
            params.destroy();
            buffer
        })
        .collect();
    // A refused buffer is a protocol error on create_immed; this roundtrip surfaces it.
    queue.roundtrip(&mut s).map_err(|e| format!("create_immed refused: {e}"))?;
    println!("DMACLIENT wl_buffers=3 created with create_immed");

    let start = Instant::now();
    let (mut commits, mut stalls) = (0u32, 0u32);
    while start.elapsed() < Duration::from_secs(seconds) && !s.closed {
        queue.dispatch_pending(&mut s).map_err(|e| err(&e))?;
        if s.configured && !s.callback_pending {
            match s.busy.iter().position(|b| !b) {
                Some(index) => {
                    buffers[index].paint(commits);
                    s.busy[index] = true;
                    s.callback_pending = true;
                    surface.frame(&q, ());
                    surface.attach(Some(&wl_buffers[index]), 0, 0);
                    surface.damage_buffer(0, 0, W as i32, H as i32);
                    surface.commit();
                    commits += 1;
                    if commits == 1 {
                        println!("DMACLIENT first commit");
                    }
                }
                None => stalls += 1, // all three held: wait for a release
            }
        }
        conn.flush().map_err(|e| err(&e))?;
        if let Some(guard) = queue.prepare_read() {
            let mut pollfd = libc::pollfd { fd: std::os::fd::AsRawFd::as_raw_fd(&conn.as_fd()), events: libc::POLLIN, revents: 0 };
            let status = unsafe { libc::poll(&mut pollfd, 1, 50) };
            if status > 0 {
                guard.read().map_err(|e| err(&e))?;
            }
        }
    }
    let elapsed = start.elapsed().as_secs_f64();
    let ok = s.callbacks > 0 && s.releases > 0;
    println!(
        "DMACLIENT done callbacks={} commits={commits} releases={} stalls={stalls} seconds={elapsed:.2} fps={:.1} mark_rgb={:02x},{:02x},{:02x} result={}",
        s.callbacks,
        s.releases,
        f64::from(s.callbacks) / elapsed,
        MARK[0],
        MARK[1],
        MARK[2],
        if ok { "ok" } else { "FAILED" }
    );
    if !ok {
        return Err("no frame callback or no buffer release observed".into());
    }
    Ok(())
}

fn main() {
    if let Err(e) = run() {
        eprintln!("darwin-wayland-dmabuf-client: {e}");
        std::process::exit(1);
    }
}
