// SPDX-License-Identifier: MIT
// A stand-in for /dev/sfspeaker-safety for the host tests: the rules of
// sf_speaker_safety.h (volumes held at 1 until a good ping, EINVAL above volume_max,
// one ETIMEDOUT after the lease is lost) plus a script of faults, and a trace of
// everything the daemon does to it.
#![allow(dead_code)]

use std::cell::RefCell;
use std::fs::OpenOptions;
use std::io::{self, Write};
use std::path::PathBuf;

use crate::daemon::device::{PdspkInfo, PdspkVolume, SpeakerDevice, SFSPK_SPEAKERS};

pub const MAGIC: u32 = 0xdec1be15;

pub struct Script {
    /// sense data of period n (frames * 8 samples); None: read() fails with EIO
    pub data: Box<dyn Fn(usize) -> Option<Vec<i16>>>,
    pub sample_rate: u32,
    /// the n-th ping (from 1) fails with this errno
    pub ping_fail: Option<(usize, i32)>,
    /// the read of period n fails with this errno
    pub read_fail: Option<(usize, i32)>,
    /// the read of period n returns 0 bytes
    pub read_eof: Option<usize>,
    /// tripped is set from the read of period n on (0: from the start)
    pub trip: Option<usize>,
    /// the lease runs out during the read of period n
    pub lease_lost: Option<usize>,
    /// SIGQUIT arrives during the read of period n (the read returns EINTR)
    pub sigquit: Option<usize>,
    /// the read of period n takes this many milliseconds
    pub stall: Option<(usize, u64)>,
    pub version: u32,
}

impl Script {
    pub fn new(data: Box<dyn Fn(usize) -> Option<Vec<i16>>>) -> Script {
        Script {
            data,
            sample_rate: 48000,
            ping_fail: None,
            read_fail: None,
            read_eof: None,
            trip: None,
            lease_lost: None,
            sigquit: None,
            stall: None,
            version: 1,
        }
    }
}

struct State {
    unlocked: bool,
    was_locked: bool,
    volume: [u32; SFSPK_SPEAKERS],
    tripped: bool,
    pings: usize,
    period: usize,
    pending: Vec<u8>,
    offset: usize,
    trace: Vec<String>,
}

pub struct FakeDevice {
    script: Script,
    state: RefCell<State>,
    trace_file: Option<PathBuf>,
}

impl FakeDevice {
    pub fn new(script: Script, trace_file: Option<PathBuf>) -> FakeDevice {
        let dev = FakeDevice {
            state: RefCell::new(State {
                unlocked: false,
                was_locked: false,
                volume: [1; SFSPK_SPEAKERS],
                tripped: script.trip == Some(0),
                pings: 0,
                period: 0,
                pending: Vec::new(),
                offset: 0,
                trace: Vec::new(),
            }),
            script,
            trace_file,
        };
        dev.log("open".to_string());
        dev
    }

    fn log(&self, line: String) {
        if let Some(path) = &self.trace_file {
            let mut f = OpenOptions::new().create(true).append(true).open(path).unwrap();
            writeln!(f, "{}", line).unwrap();
        }
        self.state.borrow_mut().trace.push(line);
    }

    pub fn trace(&self) -> Vec<String> {
        self.state.borrow().trace.clone()
    }

    fn lock(&self) {
        let mut s = self.state.borrow_mut();
        if s.unlocked {
            s.was_locked = true;
        }
        s.unlocked = false;
        s.volume = [1; SFSPK_SPEAKERS];
    }
}

impl Drop for FakeDevice {
    fn drop(&mut self) {
        self.log("close".to_string());
    }
}

impl SpeakerDevice for FakeDevice {
    fn info(&self) -> io::Result<PdspkInfo> {
        let info = {
            let s = self.state.borrow();
            PdspkInfo {
                version: self.script.version,
                sample_rate: self.script.sample_rate,
                unlocked: s.unlocked as u32,
                amp_gain: 0x0f,
                volume_max: if s.unlocked { 201 } else { 1 },
                volume: s.volume,
                die_millicelsius: [i32::MIN; SFSPK_SPEAKERS],
                tripped: s.tripped as u32,
                reserved: [0; 4],
            }
        };
        self.log(format!("info tripped={}", info.tripped));
        Ok(info)
    }

    fn ping(&self, magic: u32) -> io::Result<()> {
        let n = {
            let mut s = self.state.borrow_mut();
            s.pings += 1;
            s.pings
        };
        let err = if magic != MAGIC {
            Some(libc::EINVAL)
        } else if let Some((at, errno)) = self.script.ping_fail {
            if at == n {
                Some(errno)
            } else {
                None
            }
        } else {
            None
        };
        let err = err.or_else(|| {
            let mut s = self.state.borrow_mut();
            if s.was_locked {
                s.was_locked = false;
                Some(libc::ETIMEDOUT)
            } else {
                None
            }
        });
        if let Some(errno) = err {
            self.lock();
            self.log(format!("ping err {}", errno));
            return Err(io::Error::from_raw_os_error(errno));
        }
        self.state.borrow_mut().unlocked = true;
        self.log("ping ok".to_string());
        Ok(())
    }

    fn set_volume(&self, volume: &PdspkVolume) -> io::Result<()> {
        let max = if self.state.borrow().unlocked { 201 } else { 1 };
        let text: Vec<String> = volume.volume.iter().map(|v| v.to_string()).collect();
        if volume.volume.iter().any(|v| *v > max) {
            self.log(format!("vol {} err {}", text.join(","), libc::EINVAL));
            return Err(io::Error::from_raw_os_error(libc::EINVAL));
        }
        self.state.borrow_mut().volume = volume.volume;
        self.log(format!("vol {} ok", text.join(",")));
        Ok(())
    }

    fn read(&self, buf: &mut [u8]) -> io::Result<usize> {
        let need_period = {
            let s = self.state.borrow();
            s.offset == s.pending.len()
        };
        if need_period {
            let n = self.state.borrow().period;
            self.state.borrow_mut().period += 1;

            if self.script.trip == Some(n) {
                self.state.borrow_mut().tripped = true;
            }
            if self.script.lease_lost == Some(n) {
                self.lock();
                self.log("lease lost".to_string());
            }
            if let Some((at, ms)) = self.script.stall {
                if at == n {
                    std::thread::sleep(std::time::Duration::from_millis(ms));
                }
            }
            if self.script.sigquit == Some(n) {
                unsafe {
                    libc::raise(libc::SIGQUIT);
                }
                self.log(format!("read err {}", libc::EINTR));
                return Err(io::Error::from_raw_os_error(libc::EINTR));
            }
            if self.script.read_eof == Some(n) {
                self.log("read eof".to_string());
                return Ok(0);
            }
            let errno = match self.script.read_fail {
                Some((at, errno)) if at == n => Some(errno),
                _ => None,
            };
            let data = if errno.is_none() { (self.script.data)(n) } else { None };
            match data {
                Some(data) => {
                    let mut s = self.state.borrow_mut();
                    s.pending = data.iter().flat_map(|v| v.to_le_bytes()).collect();
                    s.offset = 0;
                }
                None => {
                    let errno = errno.unwrap_or(libc::EIO);
                    self.log(format!("read err {}", errno));
                    return Err(io::Error::from_raw_os_error(errno));
                }
            }
            self.log("read".to_string());
        }

        /* hand the period out in odd-sized pieces, as a device may */
        let mut s = self.state.borrow_mut();
        let n = buf.len().min(s.pending.len() - s.offset).min(4099);
        let offset = s.offset;
        buf[..n].copy_from_slice(&s.pending[offset..offset + n]);
        s.offset += n;
        Ok(n)
    }
}

/// The trace in the form of the reference harness's T lines.
pub fn normalize(trace: &[String]) -> Vec<String> {
    trace
        .iter()
        .filter_map(|l| {
            if l == "ping ok" {
                Some("T ping".to_string())
            } else if l == "read" {
                Some("T read".to_string())
            } else if l.starts_with("vol ") && l.ends_with(" ok") {
                Some(format!("T {}", l.trim_end_matches(" ok")))
            } else {
                None
            }
        })
        .collect()
}

/// Runs the upstream reference harness (tests/upstream-ref) and returns its lines.
pub fn upstream_ref(conf: &str, mode: &str, skip: &str) -> Vec<String> {
    let bin = std::env::var("SPEAKERSAFETYD_UPSTREAM_REF").expect(
        "SPEAKERSAFETYD_UPSTREAM_REF is not set: build tests/upstream-ref and run the tests with run-host-tests.sh",
    );
    let out = std::process::Command::new(bin)
        .args([conf, mode, skip])
        .output()
        .expect("could not run the upstream reference harness");
    assert!(out.status.success(), "upstream reference harness failed");
    String::from_utf8(out.stdout)
        .unwrap()
        .lines()
        .map(|l| l.to_string())
        .collect()
}
