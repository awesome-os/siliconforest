//! Checked POSIX PTY/session ownership. Host tests exercise host PTYs only.
//! No forkpty/login_tty: this pinned libc ignores failures inside those helpers.
use std::{
    ffi::CString,
    io,
    os::fd::{AsRawFd, FromRawFd, IntoRawFd, OwnedFd},
    time::{Duration, Instant},
};
type Result<T> = io::Result<T>;
const MAGIC: i32 = 0x44545059;
#[derive(Clone, Copy, Debug, Default)]
#[repr(C)]
struct Record {
    magic: i32,
    stage: i32,
    value: i32,
    group: i32,
}
const _: () = assert!(std::mem::size_of::<Record>() == 16);
const READY: i32 = 10;
#[derive(Default, Debug)]
struct Status {
    bytes: Vec<u8>,
    ready: bool,
    eof: bool,
    failed: bool,
}
impl Status {
    fn feed(&mut self, data: &[u8], pid: i32) -> Result<()> {
        if self.eof || self.failed || self.bytes.len() + data.len() > 64 {
            return Err(io::Error::other("invalid child status stream"));
        }
        self.bytes.extend_from_slice(data);
        while self.bytes.len() >= 16 {
            let vals: Vec<i32> = self.bytes[..16]
                .as_chunks::<4>()
                .0
                .iter()
                .map(|b| i32::from_ne_bytes(*b))
                .collect();
            self.bytes.drain(..16);
            if vals[0] != MAGIC
                || vals[1] != READY
                || vals[2] != pid
                || vals[3] != pid
                || self.ready
            {
                self.failed = true;
                return Err(io::Error::other(format!(
                    "child setup failed stage={} errno={} group={}",
                    vals[1], vals[2], vals[3]
                )));
            }
            self.ready = true;
        }
        Ok(())
    }
    fn end(&mut self) -> Result<()> {
        self.eof = true;
        if !self.ready || self.failed || !self.bytes.is_empty() {
            return Err(io::Error::other(
                "child status closed without complete setup",
            ));
        }
        Ok(())
    }
    fn exec_ok(&self) -> bool {
        self.ready && self.eof && !self.failed
    }
}
fn checked(v: i32) -> Result<i32> {
    if v < 0 {
        Err(io::Error::last_os_error())
    } else {
        Ok(v)
    }
}
fn cloexec(fd: i32) -> Result<()> {
    unsafe {
        let flags = checked(libc::fcntl(fd, libc::F_GETFD))?;
        checked(libc::fcntl(fd, libc::F_SETFD, flags | libc::FD_CLOEXEC))?;
    }
    Ok(())
}
fn nonblock(fd: i32) -> Result<()> {
    unsafe {
        let flags = checked(libc::fcntl(fd, libc::F_GETFL))?;
        checked(libc::fcntl(fd, libc::F_SETFL, flags | libc::O_NONBLOCK))?;
    }
    Ok(())
}
fn owned(fd: i32) -> Result<OwnedFd> {
    checked(fd)?;
    promote(unsafe { OwnedFd::from_raw_fd(fd) })
}
fn close_owned(fd: OwnedFd) -> Result<()> {
    // Never retry close after EINTR: descriptor state can differ by platform.
    checked(unsafe { libc::close(fd.into_raw_fd()) })?;
    Ok(())
}
fn promote(fd: OwnedFd) -> Result<OwnedFd> {
    // Always keep internal descriptors above stdio, including when caller closed it.
    if fd.as_raw_fd() < 3 {
        let next = unsafe { libc::fcntl(fd.as_raw_fd(), libc::F_DUPFD, 3) };
        checked(next)?;
        let n = unsafe { OwnedFd::from_raw_fd(next) };
        cloexec(n.as_raw_fd())?;
        close_owned(fd)?;
        Ok(n)
    } else {
        cloexec(fd.as_raw_fd())?;
        Ok(fd)
    }
}
#[cfg(target_vendor = "apple")]
unsafe fn errno() -> i32 {
    *libc::__error()
}
#[cfg(not(target_vendor = "apple"))]
unsafe fn errno() -> i32 {
    *libc::__errno_location()
}
// Only async-signal-safe operations below fork. No allocation, destructor, or stdio.
unsafe fn emit(fd: i32, r: Record) {
    let p = &r as *const Record as *const u8;
    let mut n = 0;
    while n < 16 {
        let v = libc::write(fd, p.add(n).cast(), 16 - n);
        if v > 0 {
            n += v as usize;
        } else if v < 0 && errno() == libc::EINTR {
            continue;
        } else {
            libc::_exit(126);
        }
    }
}
unsafe fn fail(fd: i32, stage: i32) -> ! {
    emit(
        fd,
        Record {
            magic: MAGIC,
            stage,
            value: errno(),
            group: 0,
        },
    );
    libc::_exit(126)
}
unsafe fn child(
    slave: i32,
    pipe: i32,
    maxfd: i32,
    path: *const libc::c_char,
    argv: *const *const libc::c_char,
    env: *const *const libc::c_char,
    inject: i32,
) -> ! {
    // Fault injection is reachable only from the cfg(test) constructor.
    if inject == 1 {
        fail(pipe, 1);
    }
    if libc::setsid() < 0 {
        fail(pipe, 1);
    }
    if inject == 2 || libc::ioctl(slave, libc::TIOCSCTTY, 0) < 0 {
        fail(pipe, 2);
    }
    for fd in 0..3 {
        if inject == 3 || libc::dup2(slave, fd) < 0 || libc::fcntl(fd, libc::F_SETFD, 0) < 0 {
            fail(pipe, 3);
        }
    }
    let pid = libc::getpid();
    let sid = libc::getsid(0);
    let group = libc::getpgrp();
    if inject == 4
        || sid != pid
        || group != pid
        || libc::tcgetpgrp(0) != pid
        || libc::isatty(0) != 1
    {
        fail(pipe, 4);
    }
    let mut set: libc::sigset_t = std::mem::zeroed();
    libc::sigemptyset(&mut set);
    if libc::sigprocmask(libc::SIG_SETMASK, &set, std::ptr::null_mut()) < 0 {
        fail(pipe, 5);
    }
    for sig in [
        libc::SIGHUP,
        libc::SIGINT,
        libc::SIGQUIT,
        libc::SIGTERM,
        libc::SIGPIPE,
        libc::SIGCHLD,
        libc::SIGTSTP,
        libc::SIGTTIN,
        libc::SIGTTOU,
    ] {
        if libc::signal(sig, libc::SIG_DFL) == libc::SIG_ERR {
            fail(pipe, 5);
        }
    }
    for fd in 3..maxfd {
        if fd != pipe {
            libc::close(fd);
        }
    }
    emit(
        pipe,
        Record {
            magic: MAGIC,
            stage: READY,
            value: sid,
            group,
        },
    );
    libc::execve(path, argv, env);
    fail(pipe, 6)
}

#[derive(Debug, Default, Clone)]
pub struct Cleanup {
    pub complete: bool,
    pub status: Option<i32>,
    pub term_sent: bool,
    pub kill_sent: bool,
    pub error: Option<String>,
}
pub struct Pty {
    master: Option<OwnedFd>,
    status_fd: Option<OwnedFd>,
    pid: i32,
    status: Status,
    pub eof: bool,
    pub received: usize,
    started: Instant,
    cleanup: Option<Cleanup>,
}
impl Pty {
    pub fn shell(cols: u16, rows: u16) -> Result<Self> {
        Self::spawn("/bin/sh", &["sh", "-i", "+m"], cols, rows, 0)
    }
    fn spawn(path: &str, args: &[&str], cols: u16, rows: u16, inject: i32) -> Result<Self> {
        if cols == 0 || rows == 0 {
            return Err(io::Error::other("zero PTY size"));
        }
        let path = CString::new(path)?;
        let args: Vec<CString> = args
            .iter()
            .map(|s| CString::new(*s))
            .collect::<std::result::Result<_, _>>()?;
        let mut argv: Vec<*const libc::c_char> = args.iter().map(|s| s.as_ptr()).collect();
        argv.push(std::ptr::null());
        let env: Vec<CString> = [
            "TERM=dumb",
            "LC_ALL=C",
            "PATH=/bin:/usr/bin:/sbin:/usr/sbin",
            "HOME=/private/tmp",
            "ENV=/dev/null",
            "ZDOTDIR=/nonexistent-dl11",
            "HISTFILE=/dev/null",
            "PS1=$ ",
            "PS2=> ",
        ]
        .iter()
        .map(|s| CString::new(*s).unwrap())
        .collect();
        let mut envp: Vec<*const libc::c_char> = env.iter().map(|s| s.as_ptr()).collect();
        envp.push(std::ptr::null());
        let maxfd = unsafe { libc::sysconf(libc::_SC_OPEN_MAX) };
        if !(3..=1048576).contains(&maxfd) {
            return Err(io::Error::other("unsupported descriptor close bound"));
        }
        let master = owned(unsafe { libc::posix_openpt(libc::O_RDWR | libc::O_NOCTTY) })?;
        checked(unsafe { libc::grantpt(master.as_raw_fd()) })?;
        checked(unsafe { libc::unlockpt(master.as_raw_fd()) })?;
        let mut name = [0 as libc::c_char; 128];
        // Pinned Darwin supplies ptsname_r (0/-1). Linux returns an errno number.
        unsafe extern "C" {
            fn ptsname_r(fd: libc::c_int, buf: *mut libc::c_char, len: libc::size_t)
                -> libc::c_int;
        }
        let r = unsafe { ptsname_r(master.as_raw_fd(), name.as_mut_ptr(), name.len()) };
        if r != 0 || !name.contains(&0) || name[0] == 0 {
            return Err(if r > 0 {
                io::Error::from_raw_os_error(r)
            } else {
                io::Error::last_os_error()
            });
        }
        let slave = owned(unsafe { libc::open(name.as_ptr(), libc::O_RDWR | libc::O_NOCTTY) })?;
        let mut term: libc::termios = unsafe { std::mem::zeroed() };
        checked(unsafe { libc::tcgetattr(slave.as_raw_fd(), &mut term) })?;
        term.c_iflag = libc::ICRNL;
        term.c_oflag = libc::OPOST | libc::ONLCR;
        term.c_lflag =
            libc::ICANON | libc::ISIG | libc::IEXTEN | libc::ECHO | libc::ECHOE | libc::ECHOK;
        term.c_cflag = (term.c_cflag & !(libc::CSIZE | libc::PARENB)) | libc::CS8 | libc::CREAD;
        for (index, value) in [
            (libc::VINTR, 3),
            (libc::VQUIT, 28),
            (libc::VERASE, 127),
            (libc::VKILL, 21),
            (libc::VEOF, 4),
            (libc::VSUSP, 26),
        ] {
            term.c_cc[index] = value;
        }
        checked(unsafe { libc::tcsetattr(slave.as_raw_fd(), libc::TCSANOW, &term) })?;
        let size = libc::winsize {
            ws_col: cols,
            ws_row: rows,
            ws_xpixel: 0,
            ws_ypixel: 0,
        };
        checked(unsafe { libc::ioctl(slave.as_raw_fd(), libc::TIOCSWINSZ, &size) })?;
        nonblock(master.as_raw_fd())?;
        let mut pipes = [-1; 2];
        checked(unsafe { libc::pipe(pipes.as_mut_ptr()) })?;
        // Own both raw pipe ends before any fallible operation.
        let read = unsafe { OwnedFd::from_raw_fd(pipes[0]) };
        let write = unsafe { OwnedFd::from_raw_fd(pipes[1]) };
        let read = promote(read)?;
        let write = promote(write)?;
        nonblock(read.as_raw_fd())?;
        let pid = unsafe { libc::fork() };
        checked(pid)?;
        if pid == 0 {
            unsafe {
                child(
                    slave.as_raw_fd(),
                    write.as_raw_fd(),
                    maxfd as i32,
                    path.as_ptr(),
                    argv.as_ptr(),
                    envp.as_ptr(),
                    inject,
                )
            }
        }
        let state = Self {
            master: Some(master),
            status_fd: Some(read),
            pid,
            status: Status::default(),
            eof: false,
            received: 0,
            started: Instant::now(),
            cleanup: None,
        };
        // state is already an owner: any parent-close error still stops/reaps child.
        close_owned(slave)?;
        close_owned(write)?;
        Ok(state)
    }
    pub fn pid(&self) -> i32 {
        self.pid
    }
    pub fn ready(&self) -> bool {
        self.status.exec_ok() && self.received > 0 && !self.eof && self.cleanup.is_none()
    }
    pub fn exec_checked(&self) -> bool {
        self.status.exec_ok()
    }
    pub fn fd(&self) -> i32 {
        self.master.as_ref().map_or(-1, AsRawFd::as_raw_fd)
    }
    pub fn setup(&mut self) -> Result<()> {
        if self.status.exec_ok() {
            return Ok(());
        }
        let Some(fd) = self.status_fd.as_ref() else {
            return Err(io::Error::other("missing child status"));
        };
        let mut b = [0; 32];
        loop {
            let n = unsafe { libc::read(fd.as_raw_fd(), b.as_mut_ptr().cast(), b.len()) };
            if n > 0 {
                self.status.feed(&b[..n as usize], self.pid)?;
            } else if n == 0 {
                self.status.end()?;
                if let Some(fd) = self.status_fd.take() {
                    close_owned(fd)?;
                }
                return Ok(());
            } else {
                let e = io::Error::last_os_error();
                if e.kind() == io::ErrorKind::Interrupted {
                    continue;
                }
                if e.kind() == io::ErrorKind::WouldBlock {
                    break;
                }
                return Err(e);
            }
        }
        if self.started.elapsed() > Duration::from_secs(5) {
            return Err(io::Error::new(
                io::ErrorKind::TimedOut,
                "child setup/exec deadline",
            ));
        }
        Ok(())
    }
    pub fn read(&mut self, b: &mut [u8]) -> Result<usize> {
        let n = unsafe { libc::read(self.fd(), b.as_mut_ptr().cast(), b.len()) };
        if n >= 0 {
            self.eof |= n == 0;
            self.received += n as usize;
            Ok(n as usize)
        } else {
            let e = io::Error::last_os_error();
            // Linux signals PTY slave close with EIO; Darwin EOF/revocation is separate.
            #[cfg(not(target_vendor = "apple"))]
            if e.raw_os_error() == Some(libc::EIO) {
                self.eof = true;
                return Ok(0);
            }
            Err(e)
        }
    }
    pub fn write(&mut self, b: &[u8]) -> Result<usize> {
        if !self.ready() {
            return Err(io::Error::other("PTY not ready for input"));
        }
        let group = unsafe { libc::tcgetpgrp(self.fd()) };
        if group != self.pid {
            return Err(io::Error::other(
                "foreground process group changed; qualification forbids job control",
            ));
        }
        let n = unsafe { libc::write(self.fd(), b.as_ptr().cast(), b.len()) };
        if n < 0 {
            Err(io::Error::last_os_error())
        } else {
            Ok(n as usize)
        }
    }
    pub fn resize(&self, cols: u16, rows: u16) -> Result<()> {
        let size = libc::winsize {
            ws_col: cols,
            ws_row: rows,
            ws_xpixel: 0,
            ws_ypixel: 0,
        };
        checked(unsafe { libc::ioctl(self.fd(), libc::TIOCSWINSZ, &size) })?;
        Ok(())
    }
    pub fn cleanup(&mut self) -> Cleanup {
        if let Some(r) = &self.cleanup {
            return r.clone();
        }
        let mut r = Cleanup::default();
        // Child is still unreaped, so its PID cannot have been recycled. If it has
        // become a session leader, kill that owned group, otherwise only this child.
        if self.pid <= 1 {
            r.error = Some("invalid child identity".into());
            self.cleanup = Some(r.clone());
            return r;
        }
        for (sig, sent) in [
            (libc::SIGTERM, &mut r.term_sent),
            (libc::SIGKILL, &mut r.kill_sent),
        ] {
            // Recheck at KILL too: a pre-setup child can execute setsid during TERM grace.
            let grouped = unsafe { libc::getsid(self.pid) } == self.pid
                && unsafe { libc::getpgid(self.pid) } == self.pid;
            let target = if grouped { -self.pid } else { self.pid };
            let rc = unsafe { libc::kill(target, sig) };
            *sent = rc == 0;
            if rc < 0 && io::Error::last_os_error().raw_os_error() != Some(libc::ESRCH) {
                r.error = Some(format!("signal {sig}: {}", io::Error::last_os_error()));
            }
            if sig == libc::SIGTERM {
                std::thread::sleep(Duration::from_millis(250));
            }
        }
        let end = Instant::now() + Duration::from_secs(2);
        loop {
            let mut status = 0;
            let n = unsafe { libc::waitpid(self.pid, &mut status, libc::WNOHANG) };
            if n == self.pid {
                r.status = Some(status);
                r.complete = r.error.is_none();
                break;
            }
            if n < 0 {
                let e = io::Error::last_os_error();
                if e.kind() != io::ErrorKind::Interrupted {
                    r.error = Some(format!("waitpid: {e}"));
                    break;
                }
            }
            if Instant::now() >= end {
                r.error = Some("child reap deadline; outer supervisor required".into());
                break;
            }
            std::thread::sleep(Duration::from_millis(10));
        }
        for fd in [self.master.take(), self.status_fd.take()]
            .into_iter()
            .flatten()
        {
            if let Err(error) = close_owned(fd) {
                r.error = Some(format!("owned fd close: {error}"));
            }
        }
        r.complete = r.status.is_some() && r.error.is_none();
        self.cleanup = Some(r.clone());
        r
    }
}
impl Drop for Pty {
    fn drop(&mut self) {
        self.cleanup();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn status_bytes(r: Record) -> Vec<u8> {
        [r.magic, r.stage, r.value, r.group]
            .into_iter()
            .flat_map(i32::to_ne_bytes)
            .collect()
    }
    #[test]
    fn split_exec_protocol_and_failure_after_setup() {
        let bytes = status_bytes(Record {
            magic: MAGIC,
            stage: READY,
            value: 42,
            group: 42,
        });
        for split in 0..=16 {
            let mut s = Status::default();
            s.feed(&bytes[..split], 42).unwrap();
            s.feed(&bytes[split..], 42).unwrap();
            assert!(!s.exec_ok());
            s.end().unwrap();
            assert!(s.exec_ok());
        }
        let mut s = Status::default();
        s.feed(&bytes, 42).unwrap();
        assert!(s
            .feed(
                &status_bytes(Record {
                    magic: MAGIC,
                    stage: 6,
                    value: 2,
                    group: 0
                }),
                42
            )
            .is_err());
        assert!(!s.exec_ok());
    }
    #[test]
    fn missing_partial_wrong_owner_rejected() {
        for n in 0..16 {
            let mut s = Status::default();
            let b = status_bytes(Record {
                magic: MAGIC,
                stage: READY,
                value: 42,
                group: 42,
            });
            s.feed(&b[..n], 42).unwrap();
            assert!(s.end().is_err());
        }
        let mut s = Status::default();
        assert!(s
            .feed(
                &status_bytes(Record {
                    magic: MAGIC,
                    stage: READY,
                    value: 42,
                    group: 7
                }),
                42
            )
            .is_err());
    }
    fn drive(p: &mut Pty) -> Result<Vec<u8>> {
        let mut out = Vec::new();
        let end = Instant::now() + Duration::from_secs(3);
        while Instant::now() < end {
            p.setup()?;
            let mut b = [0; 1024];
            match p.read(&mut b) {
                Ok(n) => out.extend_from_slice(&b[..n]),
                Err(e) if e.kind() == io::ErrorKind::WouldBlock => (),
                Err(e) => return Err(e),
            }
            if p.eof {
                break;
            }
            std::thread::sleep(Duration::from_millis(5));
        }
        Ok(out)
    }
    #[test]
    fn host_actual_pty_bytes_and_exit_reaped_once() {
        let mut p = Pty::spawn("/bin/sh", &["sh", "-c", "printf 'aA-Pty\\n'"], 80, 24, 0).unwrap();
        let data = drive(&mut p).unwrap();
        assert!(data.windows(6).any(|b| b == b"aA-Pty"));
        let r = p.cleanup();
        assert!(r.complete, "{r:?}");
        assert_eq!(r.status, Some(0));
        assert_eq!(p.cleanup().status, r.status);
        let mut s = 0;
        assert_eq!(unsafe { libc::waitpid(p.pid, &mut s, libc::WNOHANG) }, -1);
    }
    #[test]
    fn host_each_child_failure_is_not_exec_success_and_is_reaped() {
        for step in 1..=4 {
            let mut p = Pty::spawn("/bin/sh", &["sh", "-c", "exit 0"], 80, 24, step).unwrap();
            assert!(drive(&mut p).is_err());
            assert!(!p.ready());
            assert!(p.cleanup().complete);
        }
        let mut p = Pty::spawn("/does/not/exist", &["sh"], 80, 24, 0).unwrap();
        assert!(drive(&mut p).is_err());
        assert!(p.cleanup().complete);
    }
    #[test]
    fn host_hung_child_killed_and_internal_descriptors_cloexec() {
        let mut p = Pty::spawn(
            "/bin/sh",
            &[
                "sh",
                "-c",
                "trap '' TERM; printf ready; while :; do :; done",
            ],
            80,
            24,
            0,
        )
        .unwrap();
        let until = Instant::now() + Duration::from_secs(2);
        while !p.ready() && Instant::now() < until {
            p.setup().unwrap();
            let mut b = [0; 32];
            let _ = p.read(&mut b);
            std::thread::sleep(Duration::from_millis(5));
        }
        assert!(p.ready());
        assert_ne!(
            unsafe { libc::fcntl(p.fd(), libc::F_GETFD) } & libc::FD_CLOEXEC,
            0
        );
        let r = p.cleanup();
        assert!(r.complete && r.kill_sent, "{r:?}");
        assert!(libc::WIFSIGNALED(r.status.unwrap()));
        assert_eq!(libc::WTERMSIG(r.status.unwrap()), libc::SIGKILL);
    }
    #[test]
    fn host_silent_exec_is_not_ready_and_drop_reaps() {
        let pid;
        {
            let mut p =
                Pty::spawn("/bin/sh", &["sh", "-c", "while :; do :; done"], 80, 24, 0).unwrap();
            pid = p.pid;
            let end = Instant::now() + Duration::from_secs(2);
            while !p.status.exec_ok() && Instant::now() < end {
                p.setup().unwrap();
                std::thread::sleep(Duration::from_millis(5));
            }
            assert!(p.status.exec_ok());
            assert!(!p.ready());
            let mut term: libc::termios = unsafe { std::mem::zeroed() };
            assert_eq!(unsafe { libc::tcgetattr(p.fd(), &mut term) }, 0);
            assert_eq!(
                term.c_lflag & (libc::ICANON | libc::ISIG),
                libc::ICANON | libc::ISIG
            );
            // Leaving the error/unwind scope must invoke the same explicit cleanup.
        }
        let mut status = 0;
        assert_eq!(
            unsafe { libc::waitpid(pid, &mut status, libc::WNOHANG) },
            -1
        );
    }
    #[test]
    fn host_interactive_shell_real_roundtrip_resize_and_tty() {
        let mut p = Pty::spawn("/bin/sh", &["sh", "-i", "+m"], 80, 24, 0).unwrap();
        let end = Instant::now() + Duration::from_secs(3);
        let mut out = Vec::new();
        while !p.ready() && Instant::now() < end {
            p.setup().unwrap();
            let mut b = [0; 512];
            if let Ok(n) = p.read(&mut b) {
                out.extend_from_slice(&b[..n]);
            }
            std::thread::sleep(Duration::from_millis(5));
        }
        assert!(p.ready());
        let mut term: libc::termios = unsafe { std::mem::zeroed() };
        assert_eq!(unsafe { libc::tcgetattr(p.fd(), &mut term) }, 0);
        // Interactive shells may switch ICANON off for their own line editor.
        // Honor that; the noninteractive child test above checks our initial settings.
        assert_ne!(term.c_lflag & libc::ISIG, 0);
        p.resize(90, 30).unwrap();
        let mut ws: libc::winsize = unsafe { std::mem::zeroed() };
        assert_eq!(unsafe { libc::ioctl(p.fd(), libc::TIOCGWINSZ, &mut ws) }, 0);
        assert_eq!((ws.ws_col, ws.ws_row), (90, 30));
        let command = b"printf 'pty_%s%s\\n' actual Output; exit\r";
        let mut sent = 0;
        while sent < command.len() {
            sent += p.write(&command[sent..]).unwrap();
        }
        out.extend(drive(&mut p).unwrap());
        assert!(out
            .windows(b"\r\npty_actualOutput\r\n".len())
            .any(|b| b == b"\r\npty_actualOutput\r\n"));
        let result = p.cleanup();
        assert!(result.complete);
        assert_eq!(result.status, Some(0));
    }
}
