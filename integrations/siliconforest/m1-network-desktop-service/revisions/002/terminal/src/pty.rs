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
// libc types this request as u32 on Apple, while ioctl takes c_ulong.
#[cfg(target_vendor = "apple")]
const CTTY_REQUEST: libc::c_ulong = libc::TIOCSCTTY as libc::c_ulong;
#[cfg(not(target_vendor = "apple"))]
const CTTY_REQUEST: libc::c_ulong = libc::TIOCSCTTY;
const FD_CHECKED: i32 = 9;
const READY: i32 = 10;
#[derive(Default, Debug)]
struct Status {
    bytes: Vec<u8>,
    ready: bool,
    fd_checked: bool,
    eof: bool,
    failed: bool,
}
impl Status {
    fn feed(&mut self, data: &[u8], pid: i32, bound: i32, pipe: i32) -> Result<()> {
        if self.eof || self.failed || self.bytes.len() + data.len() > 64 {
            self.failed = true;
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
            let valid = vals[0] == MAGIC
                && match vals[1] {
                    FD_CHECKED if !self.fd_checked && !self.ready => {
                        if vals[2] == bound && vals[3] == pipe {
                            self.fd_checked = true;
                            true
                        } else {
                            false
                        }
                    }
                    READY if self.fd_checked && !self.ready => {
                        if vals[2] == pid && vals[3] == pid {
                            self.ready = true;
                            true
                        } else {
                            false
                        }
                    }
                    _ => false,
                };
            if !valid {
                self.failed = true;
                return Err(io::Error::other(format!(
                    "child setup failed stage={} errno={} group={}",
                    vals[1], vals[2], vals[3]
                )));
            }
        }
        Ok(())
    }
    fn end(&mut self) -> Result<()> {
        self.eof = true;
        if !self.fd_checked || !self.ready || self.failed || !self.bytes.is_empty() {
            self.failed = true;
            return Err(io::Error::other(
                "child status closed without complete setup",
            ));
        }
        Ok(())
    }
    fn exec_ok(&self) -> bool {
        self.fd_checked && self.ready && self.eof && !self.failed && self.bytes.is_empty()
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
// Actual post-sweep observations; no allocation/open/repair below fork.
unsafe fn fd_failure(pipe: i32, fd: i32, error: i32) -> ! {
    emit(
        pipe,
        Record {
            magic: MAGIC,
            stage: 7,
            value: error,
            group: fd,
        },
    );
    libc::_exit(126)
}
unsafe fn verify_child_fds(pipe: i32, bound: i32) {
    for fd in 0..bound {
        let flags = libc::fcntl(fd, libc::F_GETFD);
        if fd < 3 || fd == pipe {
            if flags < 0 {
                fd_failure(pipe, fd, errno());
            }
            let expected = if fd == pipe { libc::FD_CLOEXEC } else { 0 };
            if flags & libc::FD_CLOEXEC != expected {
                fd_failure(pipe, fd, libc::EINVAL);
            }
        } else if flags >= 0 {
            fd_failure(pipe, fd, libc::EBUSY);
        } else if errno() != libc::EBADF {
            fd_failure(pipe, fd, errno());
        }
    }
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
    if inject == 2 || libc::ioctl(slave, CTTY_REQUEST, 0) < 0 {
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
        if fd != pipe && !(inject == 7 && fd == slave) {
            libc::close(fd);
        }
    }
    verify_child_fds(pipe, maxfd);
    emit(
        pipe,
        Record {
            magic: MAGIC,
            stage: FD_CHECKED,
            value: maxfd,
            group: pipe,
        },
    );
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
    fd_bound: i32,
    child_exec_pipe: i32,
    pub eof: bool,
    pub received: usize,
    started: Instant,
    cleanup: Option<Cleanup>,
}
impl Pty {
    #[cfg(test)]
    pub(crate) fn fixture(path: &str, args: &[&str]) -> Result<Self> {
        Self::spawn(path, args, 80, 24, 0)
    }
    #[cfg(test)]
    pub fn shell(cols: u16, rows: u16) -> Result<Self> {
        Self::spawn("/bin/sh", &["sh", "-i", "+m"], cols, rows, 0)
    }
    pub fn shell_with_startup(
        cols: u16,
        rows: u16,
        startup: &crate::startup::Config,
    ) -> Result<Self> {
        Self::spawn_with_startup("/bin/sh", &["sh", "-i", "+m"], cols, rows, 0, Some(startup))
    }
    #[cfg(test)]
    fn spawn(path: &str, args: &[&str], cols: u16, rows: u16, inject: i32) -> Result<Self> {
        Self::spawn_with_startup(path, args, cols, rows, inject, None)
    }
    fn spawn_with_startup(
        path: &str,
        args: &[&str],
        cols: u16,
        rows: u16,
        inject: i32,
        startup: Option<&crate::startup::Config>,
    ) -> Result<Self> {
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
        let mut env: Vec<CString> = [
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
        // Fixed source-bound ENV profile only; allocations remain before fork.
        if let Some(config) = startup {
            env.retain(|entry| !entry.as_bytes().starts_with(b"ENV="));
            for entry in config.environment() {
                env.push(CString::new(entry)?);
            }
        }
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
        let child_exec_pipe = write.as_raw_fd();
        if child_exec_pipe < 3 || child_exec_pipe >= maxfd as i32 {
            return Err(io::Error::other(
                "exec-status pipe outside exact FD scan bound",
            ));
        }
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
            fd_bound: maxfd as i32,
            child_exec_pipe,
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
    pub fn pre_exec_fd_scan_bound(&self) -> Option<i32> {
        self.status.fd_checked.then_some(self.fd_bound)
    }
    pub fn exec_status_pipe_eof_observed(&self) -> bool {
        self.status.eof
    }
    pub fn foreground_group(&self) -> Result<i32> {
        let group = unsafe { libc::tcgetpgrp(self.fd()) };
        if group < 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(group)
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
                self.status.feed(
                    &b[..n as usize],
                    self.pid,
                    self.fd_bound,
                    self.child_exec_pipe,
                )?;
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
                "foreground process group changed; application forbids job control",
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
    fn setup_bytes() -> Vec<u8> {
        let mut bytes = status_bytes(Record {
            magic: MAGIC,
            stage: FD_CHECKED,
            value: 128,
            group: 7,
        });
        bytes.extend(status_bytes(Record {
            magic: MAGIC,
            stage: READY,
            value: 42,
            group: 42,
        }));
        bytes
    }
    #[test]
    fn split_fd_check_and_exec_protocol_requires_all_observations() {
        let bytes = setup_bytes();
        for split in 0..=bytes.len() {
            let mut s = Status::default();
            s.feed(&bytes[..split], 42, 128, 7).unwrap();
            s.feed(&bytes[split..], 42, 128, 7).unwrap();
            assert!(s.fd_checked && s.ready);
            assert!(!s.exec_ok());
            s.end().unwrap();
            assert!(s.exec_ok());
        }
        let mut s = Status::default();
        s.feed(&bytes, 42, 128, 7).unwrap();
        assert!(s
            .feed(
                &status_bytes(Record {
                    magic: MAGIC,
                    stage: 6,
                    value: 2,
                    group: 0
                }),
                42,
                128,
                7
            )
            .is_err());
        assert!(s.fd_checked);
        assert!(!s.exec_ok());
    }
    #[test]
    fn missing_partial_wrong_bound_pipe_and_order_fail_closed() {
        let bytes = setup_bytes();
        for n in 0..bytes.len() {
            let mut s = Status::default();
            s.feed(&bytes[..n], 42, 128, 7).unwrap();
            assert!(s.end().is_err());
        }
        for record in [
            Record {
                magic: MAGIC,
                stage: READY,
                value: 42,
                group: 42,
            },
            Record {
                magic: MAGIC,
                stage: FD_CHECKED,
                value: 129,
                group: 7,
            },
            Record {
                magic: MAGIC,
                stage: FD_CHECKED,
                value: 128,
                group: 8,
            },
        ] {
            assert!(Status::default()
                .feed(&status_bytes(record), 42, 128, 7)
                .is_err());
        }
        let mut s = Status::default();
        s.feed(&bytes[..16], 42, 128, 7).unwrap();
        assert!(s.feed(&bytes[..16], 42, 128, 7).is_err());
        let mut s = Status::default();
        s.feed(&bytes[..16], 42, 128, 7).unwrap();
        assert!(s
            .feed(
                &status_bytes(Record {
                    magic: MAGIC,
                    stage: READY,
                    value: 42,
                    group: 7
                }),
                42,
                128,
                7
            )
            .is_err());
    }
    #[test]
    fn partial_error_after_checked_setup_cannot_become_exec_success_at_eof() {
        let mut s = Status::default();
        s.feed(&setup_bytes(), 42, 128, 7).unwrap();
        s.feed(
            &status_bytes(Record {
                magic: MAGIC,
                stage: 6,
                value: 2,
                group: 0,
            })[..8],
            42,
            128,
            7,
        )
        .unwrap();
        assert!(s.end().is_err());
        assert!(s.fd_checked);
        assert!(s.eof);
        assert!(!s.exec_ok());
        assert!(s.feed(&[], 42, 128, 7).is_err());
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
    fn host_real_pre_exec_fd_check_and_exec_without_proc() {
        let sentinel = std::fs::File::open("/dev/null").unwrap();
        assert_eq!(
            unsafe { libc::fcntl(sentinel.as_raw_fd(), libc::F_SETFD, 0) },
            0
        );
        let mut p =
            Pty::spawn("/bin/sh", &["sh", "-c", "printf checked_child"], 80, 24, 0).unwrap();
        let output = drive(&mut p).unwrap();
        assert_eq!(output, b"checked_child");
        assert_eq!(
            p.pre_exec_fd_scan_bound(),
            Some(unsafe { libc::sysconf(libc::_SC_OPEN_MAX) } as i32)
        );
        assert!(p.exec_status_pipe_eof_observed());
        assert!(p.exec_checked());
        let cleanup = p.cleanup();
        assert!(cleanup.complete);
        assert_eq!(cleanup.status, Some(0));
    }
    #[test]
    fn host_live_extra_fd_cannot_become_checked_setup_or_exec() {
        // Only this cfg(test) constructor can request the deliberate slave leak.
        let mut p =
            Pty::spawn("/bin/sh", &["sh", "-c", "printf must_not_exec"], 80, 24, 7).unwrap();
        let error = drive(&mut p).unwrap_err();
        assert!(error.to_string().contains("stage=7"), "{error}");
        assert!(
            error
                .to_string()
                .contains(&format!("errno={}", libc::EBUSY)),
            "{error}"
        );
        assert!(p.pre_exec_fd_scan_bound().is_none());
        assert!(!p.exec_checked());
        let cleanup = p.cleanup();
        assert!(cleanup.complete);
        // Cleanup's deliberate TERM can race the failing child's _exit(126).
        // Keep its actual terminal wait status rather than requiring one cause.
        let raw = cleanup.status.unwrap();
        assert!(libc::WIFEXITED(raw) || libc::WIFSIGNALED(raw));
    }
    #[test]
    fn host_exec_failure_retains_pre_exec_fd_fact_without_exec_success() {
        let mut p = Pty::spawn("/does/not/exist", &["sh"], 80, 24, 0).unwrap();
        let error = drive(&mut p).unwrap_err();
        assert!(error.to_string().contains("stage=6"));
        assert!(p.pre_exec_fd_scan_bound().is_some());
        assert!(!p.exec_checked());
        assert!(p.cleanup().complete);
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
    #[cfg(target_os = "linux")]
    fn host_exec_shell_closes_fd3_and_non_cloexec_status_log_socket_sentinels() {
        use std::os::unix::net::UnixStream;
        let log = std::fs::File::open("/dev/null").unwrap();
        let (status, peer) = UnixStream::pair().unwrap();
        let sentinels = [log.as_raw_fd(), status.as_raw_fd(), peer.as_raw_fd()];
        for fd in sentinels {
            assert_eq!(unsafe { libc::fcntl(fd, libc::F_SETFD, 0) }, 0);
        }
        let script=format!("for fd in 3 {} {} {}; do [ ! -e /proc/$$/fd/$fd ] || exit 93; done; [ -t 0 ] && [ -t 1 ] && [ -t 2 ] || exit 94; printf descriptors_closed",sentinels[0],sentinels[1],sentinels[2]);
        let mut p = Pty::spawn("/bin/sh", &["sh", "-c", &script], 80, 24, 0).unwrap();
        let output = drive(&mut p).unwrap();
        assert!(p.exec_checked());
        assert_eq!(output, b"descriptors_closed");
        let cleanup = p.cleanup();
        assert!(cleanup.complete);
        assert_eq!(cleanup.status, Some(0));
    }
    #[test]
    #[cfg(target_os = "linux")]
    fn host_actual_changed_foreground_group_refuses_input() {
        // The helper waits for its one child before announcing readiness. No
        // separate group remains alive when our owned shell helper is cleaned.
        let script="import os,signal,time\np=os.fork()\nif p==0:\n signal.signal(signal.SIGTTOU,signal.SIG_IGN)\n os.setpgid(0,0)\n os.tcsetpgrp(0,os.getpid())\n os._exit(0)\nos.waitpid(p,0)\nprint('foreground_changed',flush=True)\ntime.sleep(10)\n";
        let mut p = Pty::spawn("/usr/bin/python3", &["python3", "-c", script], 80, 24, 0).unwrap();
        let until = Instant::now() + Duration::from_secs(3);
        while !p.ready() && Instant::now() < until {
            p.setup().unwrap();
            let mut bytes = [0; 512];
            let _ = p.read(&mut bytes);
            std::thread::sleep(Duration::from_millis(5));
        }
        assert!(p.ready());
        assert_ne!(unsafe { libc::tcgetpgrp(p.fd()) }, p.pid());
        assert!(p
            .write(b"must_refuse")
            .unwrap_err()
            .to_string()
            .contains("foreground process group changed"));
        assert!(p.cleanup().complete);
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
