//! Real Wayland observer for explicitly host-origin SFSerialHID input.
use std::{fs::OpenOptions, io::{Seek, SeekFrom, Write}, os::{fd::AsFd, unix::{fs::OpenOptionsExt, net::UnixStream}}, time::{Duration, Instant}};
use wayland_client::{Connection, Dispatch, QueueHandle, delegate_noop,
    protocol::{wl_registry, wl_compositor, wl_shm, wl_shm_pool, wl_buffer, wl_surface, wl_callback, wl_seat, wl_keyboard, wl_pointer}};
use wayland_protocols::xdg::shell::client::{xdg_wm_base, xdg_surface, xdg_toplevel};
use wayland_protocols::xdg::decoration::zv1::client::{zxdg_decoration_manager_v1, zxdg_toplevel_decoration_v1};
mod negotiation;
use negotiation::Negotiation;
mod input_proof;
mod input_events;
use input_proof::InputProof;
const W: usize = 800;
const H: usize = 480;

#[derive(Default)]
struct State {
    compositor: Option<wl_compositor::WlCompositor>,
    shm: Option<wl_shm::WlShm>, shell: Option<xdg_wm_base::XdgWmBase>,
    decoration_manager: Option<zxdg_decoration_manager_v1::ZxdgDecorationManagerV1>,
    negotiation: Negotiation, released: bool, frames: u32, closed: bool,
    seat: Option<wl_seat::WlSeat>, keyboard: Option<wl_keyboard::WlKeyboard>,
    pointer: Option<wl_pointer::WlPointer>, own_surface: Option<wl_surface::WlSurface>,
    input: InputProof, nonce: String,
}
impl Dispatch<wl_registry::WlRegistry, ()> for State {
    fn event(s: &mut Self, r: &wl_registry::WlRegistry, e: wl_registry::Event, _: &(), _: &Connection, q: &QueueHandle<Self>) {
        if let wl_registry::Event::Global { name, interface, version } = e {
            match interface.as_str() {
                "wl_compositor" => s.compositor = Some(r.bind(name, version.min(4), q, ())),
                "wl_shm" => s.shm = Some(r.bind(name, 1, q, ())),
                "xdg_wm_base" => s.shell = Some(r.bind(name, version.min(6), q, ())),
                "zxdg_decoration_manager_v1" => s.decoration_manager = Some(r.bind(name, 1, q, ())),
                "wl_seat" => {
                    if version < 5 || s.seat.is_some() { s.input.reject("ambiguous seat or missing pointer-frame protocol version"); }
                    else { s.seat=Some(r.bind(name,version.min(9),q,())); }
                }, _ => (),
            }
        }
    }
}
impl Dispatch<xdg_wm_base::XdgWmBase, ()> for State {
    fn event(_: &mut Self, p: &xdg_wm_base::XdgWmBase, e: xdg_wm_base::Event, _: &(), _: &Connection, _: &QueueHandle<Self>) {
        if let xdg_wm_base::Event::Ping { serial } = e { p.pong(serial); }
    }
}
impl Dispatch<xdg_surface::XdgSurface, ()> for State {
    fn event(s: &mut Self, p: &xdg_surface::XdgSurface, e: xdg_surface::Event, _: &(), _: &Connection, _: &QueueHandle<Self>) {
        if let xdg_surface::Event::Configure { serial } = e {
            p.ack_configure(serial);
            s.negotiation.surface_configured(serial);
            if s.negotiation.ready() { s.input.ssd(); }
        }
    }
}
impl Dispatch<zxdg_toplevel_decoration_v1::ZxdgToplevelDecorationV1, ()> for State {
    fn event(s: &mut Self, _: &zxdg_toplevel_decoration_v1::ZxdgToplevelDecorationV1,
        e: zxdg_toplevel_decoration_v1::Event, _: &(), _: &Connection, _: &QueueHandle<Self>) {
        if let zxdg_toplevel_decoration_v1::Event::Configure { mode } = e {
            // Decoration state is applied only with the subsequent xdg_surface configure.
            s.negotiation.decoration_configured(mode);
        }
    }
}
impl Dispatch<xdg_toplevel::XdgToplevel, ()> for State {
    fn event(s: &mut Self, _: &xdg_toplevel::XdgToplevel, e: xdg_toplevel::Event, _: &(), _: &Connection, _: &QueueHandle<Self>) {
        if let xdg_toplevel::Event::Close = e { s.closed = true; }
    }
}
impl Dispatch<wl_buffer::WlBuffer, ()> for State {
    fn event(s: &mut Self, _: &wl_buffer::WlBuffer, e: wl_buffer::Event, _: &(), _: &Connection, _: &QueueHandle<Self>) {
        if let wl_buffer::Event::Release = e { s.released = true; }
    }
}
impl Dispatch<wl_callback::WlCallback, ()> for State {
    fn event(s: &mut Self, _: &wl_callback::WlCallback, e: wl_callback::Event, _: &(), _: &Connection, _: &QueueHandle<Self>) {
        if let wl_callback::Event::Done { .. } = e { s.frames += 1; s.input.frame(); }
    }
}
delegate_noop!(State: ignore wl_compositor::WlCompositor);
delegate_noop!(State: ignore wl_surface::WlSurface);
delegate_noop!(State: ignore wl_shm::WlShm);
delegate_noop!(State: ignore wl_shm_pool::WlShmPool);
delegate_noop!(State: ignore zxdg_decoration_manager_v1::ZxdgDecorationManagerV1);

fn glyph(c: char) -> [u8; 7] {
    match c.to_ascii_uppercase() {
        'A'=>[14,17,17,31,17,17,17], 'B'=>[30,17,17,30,17,17,30],
        'C'=>[14,17,16,16,16,17,14], 'D'=>[30,17,17,17,17,17,30],
        'E'=>[31,16,16,30,16,16,31], 'F'=>[31,16,16,30,16,16,16],
        'G'=>[14,17,16,23,17,17,14], 'H'=>[17,17,17,31,17,17,17],
        'I'=>[14,4,4,4,4,4,14], 'J'=>[7,2,2,2,18,18,12],
        'K'=>[17,18,20,24,20,18,17], 'L'=>[16,16,16,16,16,16,31],
        'M'=>[17,27,21,21,17,17,17], 'N'=>[17,25,21,19,17,17,17],
        'O'=>[14,17,17,17,17,17,14], 'P'=>[30,17,17,30,16,16,16],
        'Q'=>[14,17,17,17,21,18,13], 'R'=>[30,17,17,30,20,18,17],
        'S'=>[15,16,16,14,1,1,30], 'T'=>[31,4,4,4,4,4,4],
        'U'=>[17,17,17,17,17,17,14], 'V'=>[17,17,17,17,17,10,4],
        'W'=>[17,17,17,21,21,21,10], 'X'=>[17,17,10,4,10,17,17],
        'Y'=>[17,17,10,4,4,4,4], 'Z'=>[31,1,2,4,8,16,31],
        '0'=>[14,17,19,21,25,17,14], '1'=>[4,12,4,4,4,4,14],
        '2'=>[14,17,1,2,4,8,31], '3'=>[30,1,1,14,1,1,30],
        '4'=>[2,6,10,18,31,2,2], '5'=>[31,16,16,30,1,1,30],
        '6'=>[14,16,16,30,17,17,14], '7'=>[31,1,2,4,8,8,8],
        '8'=>[14,17,17,14,17,17,14], '9'=>[14,17,17,15,1,1,14],
        '.'=>[0,0,0,0,0,12,12], '-'=>[0,0,0,31,0,0,0],
        '/'=>[1,1,2,4,8,16,16], ':'=>[0,12,12,0,12,12,0], _=>[0;7],
    }
}
fn rect(p: &mut [u8], x: usize, y: usize, w: usize, h: usize, rgb: [u8; 3]) {
    for yy in y..(y+h).min(H) { for xx in x..(x+w).min(W) {
        let i=(yy*W+xx)*4; p[i..i+4].copy_from_slice(&[rgb[2],rgb[1],rgb[0],255]);
    }}
}
fn text(p: &mut [u8], x: usize, y: usize, scale: usize, s: &str) {
    for (i,c) in s.chars().enumerate() { for (row,bits) in glyph(c).iter().enumerate() { for col in 0..5 {
        if bits & (1 << (4-col)) != 0 { rect(p,x+(i*6+col)*scale,y+row*scale,scale,scale,[235,242,245]); }
    }}}
}
fn run() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.len()!=6 || args[0]!="--socket" || args[2]!="--nonce" || args[4]!="--seconds" { return Err("expected --socket PATH --nonce HEX48 --seconds N".into()); }
    let nonce=&args[3]; let seconds:u64=args[5].parse()?;
    if nonce.len()!=48 || !nonce.bytes().all(|b|b.is_ascii_hexdigit()) || !(1..=120).contains(&seconds) { return Err("invalid nonce or duration".into()); }
    let conn=Connection::from_socket(UnixStream::connect(&args[1])?)?;
    let mut queue=conn.new_event_queue::<State>(); let q=queue.handle();
    conn.display().get_registry(&q,()); let mut s=State{nonce:nonce.clone(),..State::default()}; queue.roundtrip(&mut s)?;
    if let Some(error)=s.input.error() { return Err(error.into()); }
    if s.seat.is_none() { return Err("host-origin input test requires advertised wl_seat".into()); }
    let manager=s.decoration_manager.as_ref().ok_or("SSD required: no zxdg_decoration_manager_v1")?;
    let surface=s.compositor.as_ref().ok_or("no compositor")?.create_surface(&q,());
    s.own_surface=Some(surface.clone());
    let xdg=s.shell.as_ref().ok_or("no xdg shell")?.get_xdg_surface(&surface,&q,());
    let top=xdg.get_toplevel(&q,()); top.set_title("Chonkstep on M1 - real Wayland client".into());
    top.set_app_id("org.darwinlab.desktop-test-input".into());
    // Version 1 requires creation before any buffer is attached or committed.
    let decoration=manager.get_toplevel_decoration(&top,&q,());
    decoration.set_mode(zxdg_toplevel_decoration_v1::Mode::ServerSide);
    surface.commit(); // empty initial commit; no client pixels before both configures
    let path=format!("/private/tmp/dl10-client-input-{}.shm",std::process::id());
    let mut file=OpenOptions::new().read(true).write(true).create_new(true).mode(0o600).open(&path)?;
    std::fs::remove_file(&path)?; file.set_len((W*H*4) as u64)?;
    let pool=s.shm.as_ref().ok_or("no shm")?.create_pool(file.as_fd(),(W*H*4) as i32,&q,());
    let buffer=pool.create_buffer(0,W as i32,H as i32,(W*4) as i32,wl_shm::Format::Xrgb8888,&q,());
    let start=Instant::now(); let mut last=Instant::now()-Duration::from_secs(1);
    let mut paints=0; let mut last_frames=0; let mut ssd_announced=false;
    let mut input_announced=false; s.released=true;
    while start.elapsed()<Duration::from_secs(seconds) && !s.closed {
        queue.dispatch_pending(&mut s)?;
        if let Some(error)=s.negotiation.error() { return Err(error.into()); }
        s.input.tick(start.elapsed().as_millis() as u64);
        if let Some(error)=s.input.error() { return Err(error.into()); }
        if s.input.arm(start.elapsed().as_millis() as u64) {
            println!("DL10_INPUT_READY {nonce} origin=host_sfserialhid capabilities=keyboard_pointer keymap=xkb_v1 focus=own_surface modifiers=0 point=200,100");
        }
        if s.input.complete() && !input_announced {
            println!("DL10_INPUT_OBSERVED {nonce} origin=host_sfserialhid key=30 presses=1 releases=1 button=272 presses=1 releases=1 point=200,100 tolerance=1");
            input_announced=true;
        }
        if s.negotiation.ready() && !ssd_announced {
            println!("DL10_SSD_CONFIGURED {nonce} mode=server_side serial={}",s.negotiation.serial().unwrap());
            ssd_announced=true;
        }
        if s.frames!=last_frames { last_frames=s.frames; println!("DL10_CLIENT_FRAME {nonce} {}",s.frames); }
        if s.negotiation.ready() && s.released && (paints==0 || s.frames>0) && last.elapsed()>=Duration::from_millis(500) {
            let mut pixels=vec![0;W*H*4]; rect(&mut pixels,0,0,W,H,[34,47,56]);
            text(&mut pixels,24,26,4,"CHONKSTEP ON M1"); text(&mut pixels,24,90,3,"DARWIN / ARM64 / WAYLAND");
            text(&mut pixels,24,148,2,"FRESH HOST NONCE:"); text(&mut pixels,24,178,3,&nonce[..24]);
            text(&mut pixels,24,211,3,&nonce[24..]); text(&mut pixels,24,268,3,&format!("FRAME {paints}"));
            for (n,color) in [[230,60,50],[40,220,70],[45,100,245]].iter().enumerate() { rect(&mut pixels,24+n*250,324,220,48,*color); }
            rect(&mut pixels,24+(paints as usize*13)%680,409,60,24,[240,205,60]);
            file.seek(SeekFrom::Start(0))?; file.write_all(&pixels)?;
            s.released=false; surface.frame(&q,()); surface.attach(Some(&buffer),0,0);
            surface.damage_buffer(0,0,W as i32,H as i32); surface.commit(); conn.flush()?;
            paints+=1;last=Instant::now();
        }
        if let Some(guard)=queue.prepare_read() {
            conn.flush()?;
            let mut pollfd=libc::pollfd{fd:std::os::fd::AsRawFd::as_raw_fd(&conn.as_fd()),events:libc::POLLIN,revents:0};
            let status=unsafe{libc::poll(&mut pollfd,1,50)};
            if status<0 { let e=std::io::Error::last_os_error();if e.kind()!=std::io::ErrorKind::Interrupted{return Err(e.into());} }
            else if status>0 { guard.read()?; }
        }
    }
    // Protocol object order matters: decoration must precede its toplevel.
    decoration.destroy(); top.destroy(); xdg.destroy(); surface.destroy(); conn.flush()?;
    if !ssd_announced { return Err("no acknowledged server-side decoration configure before deadline/close".into()); }
    if !s.input.complete() { return Err("host-origin input proof incomplete at deadline/close".into()); }
    println!("DL10_CLIENT_DONE {nonce} callbacks={} paints={paints}",s.frames);
    if s.frames==0 { return Err("no compositor frame callback observed".into()); }
    Ok(())
}
fn main() { if let Err(e)=run(){eprintln!("darwin-wayland-client-input: {e}");std::process::exit(1);} }
