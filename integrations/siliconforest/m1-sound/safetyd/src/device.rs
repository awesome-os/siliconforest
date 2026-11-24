// SPDX-License-Identifier: MIT
// (C) 2022 The Asahi Linux Contributors
//
// Darwin port (SiliconForest, J293): this file is NEW, it has no upstream
// counterpart. It is the whole platform layer: the kernel device /dev/sfspeaker-safety
// of integrations/siliconforest/m1-sound/include/sf_speaker_safety.h, which replaces the
// ALSA control device and the ALSA sense PCM of the Linux daemon. The device sits
// behind the SpeakerDevice trait so that host tests can put a fake in its place.

use std::ffi::CString;
use std::io;

pub const SFSPK_DEVICE: &str = "/dev/sfspeaker-safety";
pub const SFSPK_SPEAKERS: usize = 4;
pub const SFSPK_SENSE_CHANNELS: usize = 8;
pub const SFSPK_VERSION: u32 = 1;

/// Speaker order of the kernel device (sf_speaker_safety.h); the names are the ALSA
/// control name prefixes of the Linux daemon (the config's [Speaker/<name>] sections).
pub const SFSPK_SPEAKER_NAMES: [&str; SFSPK_SPEAKERS] =
    ["Left Front", "Right Front", "Left Rear", "Right Rear"];

/// struct sfspk_info
#[repr(C)]
#[derive(Debug, Default, Copy, Clone)]
pub struct PdspkInfo {
    pub version: u32,
    pub sample_rate: u32,
    pub unlocked: u32,
    pub amp_gain: u32,
    pub volume_max: u32,
    pub volume: [u32; SFSPK_SPEAKERS],
    pub die_millicelsius: [i32; SFSPK_SPEAKERS],
    pub tripped: u32,
    pub reserved: [u32; 4],
}

/// struct sfspk_volume
#[repr(C)]
#[derive(Debug, Default, Copy, Clone)]
pub struct PdspkVolume {
    pub volume: [u32; SFSPK_SPEAKERS],
}

/* Darwin <sys/ioccom.h> request encoding (not the Linux one). */
const IOCPARM_MASK: u32 = 0x1fff;
const IOC_OUT: u32 = 0x4000_0000;
const IOC_IN: u32 = 0x8000_0000;

const fn ioc(inout: u32, group: u8, num: u8, len: usize) -> u32 {
    inout | ((len as u32 & IOCPARM_MASK) << 16) | ((group as u32) << 8) | num as u32
}

pub const SFSPK_IOC_INFO: u32 = ioc(IOC_OUT, b'S', 1, core::mem::size_of::<PdspkInfo>());
pub const SFSPK_IOC_PING: u32 = ioc(IOC_IN, b'S', 2, core::mem::size_of::<u32>());
pub const SFSPK_IOC_SET_VOLUME: u32 =
    ioc(IOC_IN, b'S', 3, core::mem::size_of::<PdspkVolume>());

/**
    What the daemon needs from the kernel. One implementation talks to the device,
    the host tests supply another.
*/
pub trait SpeakerDevice {
    /// SFSPK_IOC_INFO
    fn info(&self) -> io::Result<PdspkInfo>;
    /// SFSPK_IOC_PING ("Speaker Volume Unlock")
    fn ping(&self, magic: u32) -> io::Result<()>;
    /// SFSPK_IOC_SET_VOLUME (the four "Speaker Playback Volume" controls)
    fn set_volume(&self, volume: &PdspkVolume) -> io::Result<()>;
    /// read() of the sense stream; returns the number of bytes read
    fn read(&self, buf: &mut [u8]) -> io::Result<usize>;
}

/// The real device. Opening it is what the ELEM_LOCKs are on Linux: the kernel allows
/// one opener, and closing it (also by dying) locks the volumes at once.
pub struct SfSpeaker {
    fd: libc::c_int,
}

impl SfSpeaker {
    pub fn open(path: &str) -> io::Result<SfSpeaker> {
        let cpath = CString::new(path).map_err(|_| io::Error::from_raw_os_error(libc::EINVAL))?;
        let fd = unsafe { libc::open(cpath.as_ptr(), libc::O_RDWR | libc::O_CLOEXEC) };
        if fd < 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(SfSpeaker { fd })
    }

    fn ioctl<T>(&self, request: u32, arg: *mut T) -> io::Result<()> {
        if unsafe { libc::ioctl(self.fd, request as _, arg) } < 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(())
    }
}

impl Drop for SfSpeaker {
    fn drop(&mut self) {
        unsafe {
            libc::close(self.fd);
        }
    }
}

impl SpeakerDevice for SfSpeaker {
    fn info(&self) -> io::Result<PdspkInfo> {
        let mut info: PdspkInfo = Default::default();
        self.ioctl(SFSPK_IOC_INFO, &mut info)?;
        Ok(info)
    }

    fn ping(&self, magic: u32) -> io::Result<()> {
        let mut magic = magic;
        self.ioctl(SFSPK_IOC_PING, &mut magic)
    }

    fn set_volume(&self, volume: &PdspkVolume) -> io::Result<()> {
        let mut volume = *volume;
        self.ioctl(SFSPK_IOC_SET_VOLUME, &mut volume)
    }

    fn read(&self, buf: &mut [u8]) -> io::Result<usize> {
        let ret = unsafe { libc::read(self.fd, buf.as_mut_ptr() as *mut libc::c_void, buf.len()) };
        if ret < 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(ret as usize)
    }
}
