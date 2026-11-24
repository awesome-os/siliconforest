//! Inherited private fd 3; fixed bounded status frames, never shell output.
use crate::pty::Cleanup;
use std::{
    collections::VecDeque,
    io::{self, Write},
    os::{
        fd::{AsRawFd, OwnedFd},
        unix::net::UnixStream,
    },
    time::{Duration, Instant},
};
pub const MAX_FRAME: usize = 256;
pub struct Status {
    stream: UnixStream,
    instance: String,
    pid: u32,
    pending: VecDeque<u8>,
    pending_since: Option<Instant>,
    ready: bool,
    cleanup: bool,
}
pub fn valid_instance(instance: &str) -> bool {
    instance.len() == 48
        && instance
            .bytes()
            .all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b))
}
impl Status {
    pub fn adopt(fd: OwnedFd, instance: &str) -> io::Result<Self> {
        if !valid_instance(instance) {
            return Err(io::Error::other("invalid application identity"));
        }
        let stream = UnixStream::from(fd);
        // peer_addr validates a connected AF_UNIX endpoint, SO_TYPE excludes dgram.
        stream.peer_addr()?;
        let mut kind: libc::c_int = 0;
        let mut length = std::mem::size_of_val(&kind) as libc::socklen_t;
        if unsafe {
            libc::getsockopt(
                stream.as_raw_fd(),
                libc::SOL_SOCKET,
                libc::SO_TYPE,
                (&mut kind as *mut libc::c_int).cast(),
                &mut length,
            )
        } < 0
        {
            return Err(io::Error::last_os_error());
        }
        if length as usize != std::mem::size_of_val(&kind) || kind != libc::SOCK_STREAM {
            return Err(io::Error::other(
                "status fd must be a connected Unix stream",
            ));
        }
        stream.set_nonblocking(true)?;
        let flags = unsafe { libc::fcntl(stream.as_raw_fd(), libc::F_GETFD) };
        if flags < 0
            || unsafe { libc::fcntl(stream.as_raw_fd(), libc::F_SETFD, flags | libc::FD_CLOEXEC) }
                < 0
        {
            return Err(io::Error::last_os_error());
        }
        Ok(Self {
            stream,
            instance: instance.to_owned(),
            pid: std::process::id(),
            pending: VecDeque::new(),
            pending_since: None,
            ready: false,
            cleanup: false,
        })
    }
    fn enqueue(&mut self, line: String) -> io::Result<()> {
        if line.len() > MAX_FRAME || self.pending.len() + line.len() > 2 * MAX_FRAME {
            return Err(io::Error::other("status frame bound"));
        }
        if self.pending.is_empty() {
            self.pending_since = Some(Instant::now());
        }
        self.pending.extend(line.bytes());
        self.pump()
    }
    pub fn ready(&mut self) -> io::Result<()> {
        if self.ready || self.cleanup {
            return Err(io::Error::other("duplicate/out-of-order READY"));
        }
        self.ready = true;
        self.enqueue(format!(
            "CHONKTERM/1 READY {} {}\n",
            self.instance, self.pid
        ))
    }
    pub fn cleanup(&mut self, cleanup: &Cleanup) -> io::Result<()> {
        if self.cleanup || (cleanup.complete && cleanup.status.is_none_or(|s| s < 0)) {
            return Err(io::Error::other("invalid/duplicate CLEANUP"));
        }
        self.cleanup = true;
        self.enqueue(format!(
            "CHONKTERM/1 CLEANUP {} {} {} {} {} {}\n",
            self.instance,
            self.pid,
            u8::from(cleanup.complete),
            cleanup.status.unwrap_or(-1),
            u8::from(cleanup.term_sent),
            u8::from(cleanup.kill_sent)
        ))
    }
    pub fn pump(&mut self) -> io::Result<()> {
        if self
            .pending_since
            .is_some_and(|t| t.elapsed() >= Duration::from_secs(1))
        {
            return Err(io::Error::new(
                io::ErrorKind::TimedOut,
                "status write deadline",
            ));
        }
        while !self.pending.is_empty() {
            let bytes = self.pending.make_contiguous();
            match self.stream.write(bytes) {
                Ok(0) => return Err(io::ErrorKind::WriteZero.into()),
                Ok(n) => {
                    self.pending.drain(..n);
                }
                Err(e) if e.kind() == io::ErrorKind::Interrupted => continue,
                Err(e) if e.kind() == io::ErrorKind::WouldBlock => return Ok(()),
                Err(e) => return Err(e),
            }
        }
        self.pending_since = None;
        Ok(())
    }
    pub fn finish(&mut self) -> io::Result<()> {
        let until = Instant::now() + Duration::from_millis(250);
        loop {
            self.pump()?;
            if self.pending.is_empty() {
                return Ok(());
            }
            if Instant::now() >= until {
                return Err(io::Error::new(
                    io::ErrorKind::TimedOut,
                    "final status flush deadline",
                ));
            }
            let mut fd = libc::pollfd {
                fd: self.stream.as_raw_fd(),
                events: libc::POLLOUT,
                revents: 0,
            };
            let n = unsafe { libc::poll(&mut fd, 1, 10) };
            if n < 0 && io::Error::last_os_error().kind() != io::ErrorKind::Interrupted {
                return Err(io::Error::last_os_error());
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Read;
    fn pair() -> (Status, UnixStream) {
        let (a, b) = UnixStream::pair().unwrap();
        (Status::adopt(a.into(), &"a".repeat(48)).unwrap(), b)
    }
    #[test]
    fn actual_stream_frames_and_checked_cleanup() {
        let (mut s, mut peer) = pair();
        s.ready().unwrap();
        assert!(s.ready().is_err());
        s.cleanup(&Cleanup {
            complete: true,
            status: Some(15),
            term_sent: true,
            ..Default::default()
        })
        .unwrap();
        s.finish().unwrap();
        drop(s);
        let mut text = String::new();
        peer.read_to_string(&mut text).unwrap();
        assert_eq!(
            text,
            format!(
                "CHONKTERM/1 READY {} {}\nCHONKTERM/1 CLEANUP {} {} 1 15 1 0\n",
                "a".repeat(48),
                std::process::id(),
                "a".repeat(48),
                std::process::id()
            )
        );
        assert!(text.lines().all(|line| line.len() + 1 <= MAX_FRAME));
    }
    #[test]
    fn cleanup_without_ready_and_missing_wait_status() {
        let (mut s, _peer) = pair();
        assert!(s
            .cleanup(&Cleanup {
                complete: true,
                ..Default::default()
            })
            .is_err());
        assert!(s
            .cleanup(&Cleanup {
                complete: true,
                status: Some(-1),
                ..Default::default()
            })
            .is_err());
        s.cleanup(&Cleanup::default()).unwrap();
        assert!(s.ready().is_err());
    }
    #[test]
    fn absent_wrong_type_broken_and_full_channel_are_bounded() {
        let file = std::fs::File::open("/dev/null").unwrap();
        assert!(Status::adopt(file.into(), &"0".repeat(48)).is_err());
        let (mut s, peer) = pair();
        // Other parallel PTY tests may temporarily inherit a peer between fork
        // and their close loop. Shutdown affects those duplicates as well.
        peer.shutdown(std::net::Shutdown::Both).unwrap();
        drop(peer);
        assert!(s.ready().is_err());
        let (mut s, _peer) = pair();
        let size: libc::c_int = 1024;
        assert_eq!(
            unsafe {
                libc::setsockopt(
                    s.stream.as_raw_fd(),
                    libc::SOL_SOCKET,
                    libc::SO_SNDBUF,
                    (&size as *const libc::c_int).cast(),
                    std::mem::size_of_val(&size) as _,
                )
            },
            0
        );
        while let Ok(n) = s.stream.write(&[0; 1024]) {
            assert!(n > 0);
        }
        s.ready().unwrap();
        assert!(!s.pending.is_empty());
        let before = Instant::now();
        assert!(s.finish().is_err());
        assert!(before.elapsed() < Duration::from_secs(1));
        s.pending_since = Some(Instant::now() - Duration::from_secs(2));
        assert!(s.pump().is_err());
    }
    #[test]
    fn lower_hex_only() {
        assert!(valid_instance(&"abcdef012345".repeat(4)));
        assert!(!valid_instance(&"A".repeat(48)));
        assert!(!valid_instance(&"0".repeat(47)));
    }
}
