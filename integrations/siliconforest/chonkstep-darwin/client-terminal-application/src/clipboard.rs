//! Clipboard plumbing: what is offered, what is accepted, and the pipes the
//! text travels through. Nothing here blocks: both ends are pumped from the
//! session loop.
use std::{
    fs::File,
    io::{self, Read, Write},
    os::fd::{FromRawFd, OwnedFd},
};

/// Text types offered on copy and accepted on paste, best first.
pub const MIMES: [&str; 5] = ["text/plain;charset=utf-8", "UTF8_STRING", "text/plain", "TEXT", "STRING"];
/// Longest paste taken; anything beyond is cut off.
pub const MAX_PASTE: usize = 256 * 1024;

pub fn pick(offered: &[String]) -> Option<&'static str> {
    MIMES.into_iter().find(|mime| offered.iter().any(|o| o == mime))
}

/// The bytes a paste types into the program. Control characters other than
/// tab and line ends are dropped (an escape sequence in pasted text must not
/// act on the terminal, and must not be able to end bracketed paste early);
/// line ends become carriage returns, as the Return key sends. With
/// bracketed paste on (DECSET 2004) the text goes between `ESC [ 200 ~` and
/// `ESC [ 201 ~`, so the program can tell it from typing.
pub fn paste_bytes(text: &[u8], bracketed: bool) -> Vec<u8> {
    let mut out = Vec::with_capacity(text.len() + 12);
    if bracketed {
        out.extend_from_slice(b"\x1b[200~");
    }
    let mut previous = 0u8;
    for &b in &text[..text.len().min(MAX_PASTE)] {
        match b {
            b'\n' if previous == b'\r' => {}
            b'\n' | b'\r' => out.push(b'\r'),
            b'\t' => out.push(b),
            0..=0x1f | 0x7f => {}
            _ => out.push(b),
        }
        previous = b;
    }
    if bracketed {
        out.extend_from_slice(b"\x1b[201~");
    }
    out
}

fn nonblocking_cloexec(fd: i32) -> io::Result<()> {
    unsafe {
        let flags = libc::fcntl(fd, libc::F_GETFL);
        if flags < 0 || libc::fcntl(fd, libc::F_SETFL, flags | libc::O_NONBLOCK) < 0 || libc::fcntl(fd, libc::F_SETFD, libc::FD_CLOEXEC) < 0 {
            return Err(io::Error::last_os_error());
        }
    }
    Ok(())
}

/// Text on its way to another client that asked for our selection.
pub struct Outgoing {
    file: File,
    data: Vec<u8>,
    at: usize,
}
impl Outgoing {
    pub fn new(fd: OwnedFd, data: Vec<u8>) -> io::Result<Self> {
        nonblocking_cloexec(std::os::fd::AsRawFd::as_raw_fd(&fd))?;
        Ok(Self { file: File::from(fd), data, at: 0 })
    }
    /// Write what the pipe takes. `true`: finished (or the reader went away).
    pub fn pump(&mut self) -> bool {
        while self.at < self.data.len() {
            match self.file.write(&self.data[self.at..]) {
                Ok(0) => return true,
                Ok(n) => self.at += n,
                Err(e) if e.kind() == io::ErrorKind::Interrupted => {}
                Err(e) if e.kind() == io::ErrorKind::WouldBlock => return false,
                Err(_) => return true,
            }
        }
        true
    }
}

/// A paste being read from the selection's owner.
pub struct Incoming {
    file: File,
    /// Our copy of the write end, kept until the request carrying it is flushed.
    write_end: Option<OwnedFd>,
    data: Vec<u8>,
    started: std::time::Instant,
}
impl Incoming {
    /// A pipe: the write end is for the `receive` request.
    pub fn new() -> io::Result<Self> {
        let mut fds = [0; 2];
        if unsafe { libc::pipe(fds.as_mut_ptr()) } < 0 {
            return Err(io::Error::last_os_error());
        }
        // SAFETY: both descriptors were just created and are owned here.
        let (read, write) = unsafe { (OwnedFd::from_raw_fd(fds[0]), OwnedFd::from_raw_fd(fds[1])) };
        nonblocking_cloexec(fds[0])?;
        nonblocking_cloexec(fds[1])?;
        Ok(Self { file: File::from(read), write_end: Some(write), data: Vec::new(), started: std::time::Instant::now() })
    }
    pub fn write_end(&self) -> Option<&OwnedFd> {
        self.write_end.as_ref()
    }
    /// The request has been sent: only the owner holds the write end now.
    pub fn sent(&mut self) {
        self.write_end = None;
    }
    /// Read what has arrived. `Some(text)` when the owner closed its end, the
    /// limit was reached, or it took longer than two seconds.
    pub fn pump(&mut self) -> Option<Vec<u8>> {
        if self.write_end.is_some() {
            return None;
        }
        let mut buffer = [0u8; 4096];
        loop {
            match self.file.read(&mut buffer) {
                Ok(0) => return Some(std::mem::take(&mut self.data)),
                Ok(n) => {
                    self.data.extend_from_slice(&buffer[..n]);
                    if self.data.len() >= MAX_PASTE {
                        self.data.truncate(MAX_PASTE);
                        return Some(std::mem::take(&mut self.data));
                    }
                }
                Err(e) if e.kind() == io::ErrorKind::Interrupted => {}
                Err(e) if e.kind() == io::ErrorKind::WouldBlock => {
                    return (self.started.elapsed() > std::time::Duration::from_secs(2)).then(|| std::mem::take(&mut self.data));
                }
                Err(_) => return Some(std::mem::take(&mut self.data)),
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn pasted_text_is_plain_and_bracketed_when_asked() {
        assert_eq!(paste_bytes(b"ls -l\n", false), b"ls -l\r");
        assert_eq!(paste_bytes(b"a\r\nb\rc\n\nd", false), b"a\rb\rc\r\rd");
        assert_eq!(paste_bytes(b"echo hi", true), b"\x1b[200~echo hi\x1b[201~");
        // An escape sequence in the text cannot end the bracket or act on the terminal.
        assert_eq!(paste_bytes(b"x\x1b[201~rm -rf\x07\x00\ty\x7f", true), b"\x1b[200~x[201~rm -rf\ty\x1b[201~");
        assert_eq!(paste_bytes("gr\u{fc}n".as_bytes(), false), "gr\u{fc}n".as_bytes());
        assert_eq!(paste_bytes(&vec![b'a'; MAX_PASTE + 10], false).len(), MAX_PASTE);
    }
    #[test]
    fn the_best_offered_text_type_is_taken() {
        let offered = |list: &[&str]| list.iter().map(|s| s.to_string()).collect::<Vec<_>>();
        assert_eq!(pick(&offered(&["text/html", "text/plain", "text/plain;charset=utf-8"])), Some("text/plain;charset=utf-8"));
        assert_eq!(pick(&offered(&["STRING", "image/png"])), Some("STRING"));
        assert_eq!(pick(&offered(&["image/png"])), None);
    }
    #[test]
    fn text_crosses_a_pipe_without_blocking_either_end() {
        let mut incoming = Incoming::new().unwrap();
        let write = incoming.write_end().unwrap().try_clone().unwrap();
        // Until the request is sent nothing is read (and the pipe cannot reach EOF).
        assert!(incoming.pump().is_none());
        incoming.sent();
        assert!(incoming.pump().is_none());
        // More than a pipe holds: the writer needs several turns.
        let text: Vec<u8> = (0..200_000u32).map(|i| b'a' + (i % 26) as u8).collect();
        let mut outgoing = Outgoing::new(write, text.clone()).unwrap();
        let mut got = None;
        let mut done = false;
        for _ in 0..10_000 {
            if !done && outgoing.pump() {
                done = true;
                // Dropping the writer closes the pipe: the reader sees the end.
                outgoing = Outgoing::new(Incoming::new().unwrap().write_end.take().unwrap(), Vec::new()).unwrap();
            }
            if let Some(data) = incoming.pump() {
                got = Some(data);
                break;
            }
        }
        assert!(done);
        assert_eq!(got.unwrap(), text);
    }
}
