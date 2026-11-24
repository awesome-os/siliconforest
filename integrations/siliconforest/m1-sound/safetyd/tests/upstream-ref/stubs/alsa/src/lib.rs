// SPDX-License-Identifier: MIT
// Stand-in for the alsa crate (0.11) in the host reference build: just the API that
// upstream speakersafetyd's types.rs and helpers.rs use, on a fake J293 sound card
// (the controls of macaudio + tas2770 that the daemon touches). Nothing here is used
// by the Darwin daemon.
//
// The dB conversions are NOT reimplemented: like alsa-lib's snd_ctl_convert_*_dB()
// they pass the control's TLV and its current range to snd_tlv_convert_to_dB(),
// snd_tlv_convert_from_dB() and snd_tlv_get_dB_range() of the real libasound.so.2.

use std::cell::RefCell;
use std::ffi::{CStr, CString};
use std::fmt;

use libc::{c_int, c_long, c_uint, c_void};

#[derive(Debug)]
pub struct Error(pub &'static str, pub i32);

impl Error {
    pub fn errno(&self) -> i32 {
        self.1
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "ALSA function '{}' failed with error {}", self.0, self.1)
    }
}

pub type Result<T> = std::result::Result<T, Error>;

/// Rounding mode, as in alsa-rs: Floor = 0, Ceil = 1, passed to alsa-lib as xdir.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Round {
    Floor = 0,
    Ceil = 1,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Direction {
    Playback,
    Capture,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ValueOr {
    Less = -1,
    Nearest = 0,
    Greater = 1,
}

pub mod mixer {
    #[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord)]
    pub struct MilliBel(pub i64);

    impl MilliBel {
        pub fn to_db(self) -> f32 {
            (self.0 as f32) / 100.0
        }
        pub fn from_db(db: f32) -> Self {
            MilliBel((db * 100.0) as i64)
        }
    }
}

struct LibAsound {
    to_db: unsafe extern "C" fn(*mut c_uint, c_long, c_long, c_long, *mut c_long) -> c_int,
    from_db: unsafe extern "C" fn(*mut c_uint, c_long, c_long, c_long, *mut c_long, c_int) -> c_int,
    db_range: unsafe extern "C" fn(*mut c_uint, c_long, c_long, *mut c_long, *mut c_long) -> c_int,
}

impl LibAsound {
    fn load() -> LibAsound {
        unsafe {
            let name = CString::new("libasound.so.2").unwrap();
            let lib = libc::dlopen(name.as_ptr(), libc::RTLD_NOW);
            assert!(!lib.is_null(), "libasound.so.2 not found");
            let sym = |s: &str| -> *mut c_void {
                let s = CString::new(s).unwrap();
                let p = libc::dlsym(lib, s.as_ptr());
                assert!(!p.is_null());
                p
            };
            LibAsound {
                to_db: std::mem::transmute(sym("snd_tlv_convert_to_dB")),
                from_db: std::mem::transmute(sym("snd_tlv_convert_from_dB")),
                db_range: std::mem::transmute(sym("snd_tlv_get_dB_range")),
            }
        }
    }
}

pub mod ctl {
    use super::*;
    use crate::mixer::MilliBel;

    pub const UNLOCK_MAGIC: i32 = 0xdec1be15u32 as i32;
    pub const SPEAKERS: [&str; 4] = ["Left Front", "Right Front", "Left Rear", "Right Rear"];
    const VOLUME_LOCKED: i64 = 1;
    const VOLUME_MAX: i64 = 201;

    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub enum ElemIface {
        Card,
        Mixer,
        PCM,
    }

    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub enum ElemType {
        Boolean,
        Integer,
    }

    #[derive(Debug, Clone)]
    pub struct ElemId {
        name: String,
    }

    impl ElemId {
        pub fn new(_iface: ElemIface) -> ElemId {
            ElemId {
                name: String::new(),
            }
        }
        pub fn set_name(&mut self, name: &CStr) {
            self.name = name.to_str().unwrap().to_string();
        }
    }

    pub struct ElemValue {
        name: String,
        t: ElemType,
        value: i64,
    }

    impl ElemValue {
        pub fn new(t: ElemType) -> Result<ElemValue> {
            Ok(ElemValue {
                name: String::new(),
                t,
                value: 0,
            })
        }
        pub fn set_id(&mut self, id: &ElemId) {
            self.name = id.name.clone();
        }
        pub fn get_integer(&self, idx: u32) -> Option<i32> {
            if idx != 0 || self.t != ElemType::Integer {
                return None;
            }
            Some(self.value as i32)
        }
        pub fn set_integer(&mut self, idx: u32, val: i32) -> Option<()> {
            if idx != 0 || self.t != ElemType::Integer {
                return None;
            }
            self.value = val as i64;
            Some(())
        }
        pub fn get_boolean(&self, idx: u32) -> Option<bool> {
            if idx != 0 || self.t != ElemType::Boolean {
                return None;
            }
            Some(self.value != 0)
        }
        pub fn set_boolean(&mut self, idx: u32, val: bool) -> Option<()> {
            if idx != 0 || self.t != ElemType::Boolean {
                return None;
            }
            self.value = val as i64;
            Some(())
        }
    }

    /// One control of the fake card. `tlv` is a DECLARE_TLV_DB_SCALE: [type, len, min, step|mute]
    struct Control {
        name: String,
        t: ElemType,
        value: i64,
        min: i64,
        max: i64,
        tlv: Option<[c_uint; 4]>,
        locked: bool,
    }

    fn db_scale(min: i32, step: u32, mute: bool) -> Option<[c_uint; 4]> {
        Some([
            1, /* SNDRV_CTL_TLVT_DB_SCALE */
            8,
            min as c_uint,
            (step & 0xffff) | if mute { 0x10000 } else { 0 },
        ])
    }

    /// The fake card. `trace` lists what the daemon did to it that matters:
    /// "ping" (a good write of the unlock control) and "vol a,b,c,d" (the four
    /// speaker volumes after a write to one of them).
    pub struct Ctl {
        lib: LibAsound,
        controls: RefCell<Vec<Control>>,
        pub trace: RefCell<Vec<String>>,
    }

    impl Ctl {
        pub fn new(card: &str, _nonblock: bool) -> Result<Ctl> {
            if card != "hw:AppleJ293" {
                return Err(Error("snd_ctl_open", -libc::ENOENT));
            }
            let mut controls = Vec::new();
            let mut add = |name: String, t, value, min, max, tlv| {
                controls.push(Control {
                    name,
                    t,
                    value,
                    min,
                    max,
                    tlv,
                    locked: false,
                })
            };
            for s in SPEAKERS {
                add(format!("{} VSENSE Switch", s), ElemType::Boolean, 0, 0, 1, None);
                add(format!("{} ISENSE Switch", s), ElemType::Boolean, 0, 0, 1, None);
                /* tas2770_playback_volume; locked by macaudio: platform_max = 1 */
                add(
                    format!("{} Speaker Playback Volume", s),
                    ElemType::Integer,
                    VOLUME_LOCKED,
                    0,
                    VOLUME_LOCKED,
                    db_scale(-10050, 50, false),
                );
                /* tas2770_digital_tlv; macaudio limits the J293's to 0x0f (18.5 dBV) */
                add(
                    format!("{} Amp Gain Volume", s),
                    ElemType::Integer,
                    0,
                    0,
                    0x0f,
                    db_scale(1100, 50, false),
                );
            }
            add("Speaker Sample Rate".to_string(), ElemType::Integer, 48000, 0, i32::MAX as i64, None);
            add(
                "Speaker Volume Unlock".to_string(),
                ElemType::Integer,
                0,
                i32::MIN as i64,
                i32::MAX as i64,
                None,
            );
            Ok(Ctl {
                lib: LibAsound::load(),
                controls: RefCell::new(controls),
                trace: RefCell::new(Vec::new()),
            })
        }

        fn with<T>(&self, name: &str, func: &'static str, f: impl FnOnce(&mut Control) -> Result<T>) -> Result<T> {
            let mut controls = self.controls.borrow_mut();
            match controls.iter_mut().find(|c| c.name == name) {
                Some(c) => f(c),
                None => Err(Error(func, -libc::ENOENT)),
            }
        }

        pub fn elem_lock(&self, id: &ElemId) -> Result<()> {
            self.with(&id.name, "snd_ctl_elem_lock", |c| {
                c.locked = true;
                Ok(())
            })
        }

        pub fn elem_read(&self, val: &mut ElemValue) -> Result<()> {
            let name = val.name.clone();
            self.with(&name, "snd_ctl_elem_read", |c| {
                if c.t != val.t {
                    return Err(Error("snd_ctl_elem_read", -libc::EINVAL));
                }
                val.value = c.value;
                Ok(())
            })
        }

        pub fn elem_write(&self, val: &ElemValue) -> Result<bool> {
            let changed = self.with(&val.name, "snd_ctl_elem_write", |c| {
                if c.t != val.t || !c.locked {
                    return Err(Error("snd_ctl_elem_write", -libc::EINVAL));
                }
                if c.name == "Speaker Volume Unlock" {
                    if val.value != UNLOCK_MAGIC as i64 {
                        return Err(Error("snd_ctl_elem_write", -libc::EINVAL));
                    }
                    return Ok(true);
                }
                if val.value < c.min || val.value > c.max {
                    return Err(Error("snd_ctl_elem_write", -libc::EINVAL));
                }
                let changed = c.value != val.value;
                c.value = val.value;
                Ok(changed)
            })?;

            if val.name == "Speaker Volume Unlock" {
                /* macaudio_vlimit_unlock(): the volumes may go up to 0 dB now */
                for c in self.controls.borrow_mut().iter_mut() {
                    if c.name.ends_with("Speaker Playback Volume") {
                        c.max = VOLUME_MAX;
                    }
                }
                self.trace.borrow_mut().push("ping".to_string());
            } else if val.name.ends_with("Speaker Playback Volume") {
                let controls = self.controls.borrow();
                let vols: Vec<String> = SPEAKERS
                    .iter()
                    .map(|s| {
                        let name = format!("{} Speaker Playback Volume", s);
                        controls.iter().find(|c| c.name == name).unwrap().value.to_string()
                    })
                    .collect();
                self.trace.borrow_mut().push(format!("vol {}", vols.join(",")));
            }
            Ok(changed)
        }

        fn tlv(&self, id: &ElemId, func: &'static str) -> Result<([c_uint; 4], c_long, c_long)> {
            self.with(&id.name, func, |c| match c.tlv {
                Some(tlv) => Ok((tlv, c.min as c_long, c.max as c_long)),
                None => Err(Error(func, -libc::EINVAL)),
            })
        }

        pub fn get_db_range(&self, id: &ElemId) -> Result<(MilliBel, MilliBel)> {
            let (mut tlv, rmin, rmax) = self.tlv(id, "snd_ctl_get_dB_range")?;
            let (mut min, mut max): (c_long, c_long) = (0, 0);
            let ret = unsafe { (self.lib.db_range)(tlv.as_mut_ptr(), rmin, rmax, &mut min, &mut max) };
            if ret < 0 {
                return Err(Error("snd_ctl_get_dB_range", ret));
            }
            Ok((MilliBel(min as i64), MilliBel(max as i64)))
        }

        pub fn convert_to_db(&self, id: &ElemId, volume: i64) -> Result<MilliBel> {
            let (mut tlv, rmin, rmax) = self.tlv(id, "snd_ctl_convert_to_dB")?;
            let mut db: c_long = 0;
            let ret = unsafe { (self.lib.to_db)(tlv.as_mut_ptr(), rmin, rmax, volume as c_long, &mut db) };
            if ret < 0 {
                return Err(Error("snd_ctl_convert_to_dB", ret));
            }
            Ok(MilliBel(db as i64))
        }

        pub fn convert_from_db(&self, id: &ElemId, db: MilliBel, dir: Round) -> Result<i64> {
            let (mut tlv, rmin, rmax) = self.tlv(id, "snd_ctl_convert_from_dB")?;
            let mut value: c_long = 0;
            let ret = unsafe {
                (self.lib.from_db)(tlv.as_mut_ptr(), rmin, rmax, db.0 as c_long, &mut value, dir as c_int)
            };
            if ret < 0 {
                return Err(Error("snd_ctl_convert_from_dB", ret));
            }
            Ok(value as i64)
        }

        /// Not alsa API, for the harness: convert with an explicit range and scale.
        pub fn raw_convert_from_db(&self, min: i32, step: u32, rmin: i64, rmax: i64, db: MilliBel, dir: Round) -> i64 {
            let mut tlv = db_scale(min, step, false).unwrap();
            let mut value: c_long = 0;
            let ret = unsafe {
                (self.lib.from_db)(tlv.as_mut_ptr(), rmin as c_long, rmax as c_long, db.0 as c_long, &mut value, dir as c_int)
            };
            assert!(ret >= 0);
            value as i64
        }
    }
}

/// The sense PCM is not part of the reference (the harness feeds run_model() itself);
/// these exist so that upstream's helpers::open_pcm() compiles.
pub mod pcm {
    use super::*;

    pub struct PCM;

    impl PCM {
        pub fn new(_name: &str, _dir: Direction, _nonblock: bool) -> Result<PCM> {
            Err(Error("snd_pcm_open", -libc::ENODEV))
        }
        pub fn hw_params(&self, _h: &HwParams) -> Result<()> {
            Ok(())
        }
    }

    pub struct HwParams<'a>(&'a PCM);

    impl<'a> HwParams<'a> {
        pub fn any(pcm: &'a PCM) -> Result<HwParams<'a>> {
            Ok(HwParams(pcm))
        }
        pub fn get_rate_max(&self) -> Result<u32> {
            Ok(48000)
        }
        pub fn get_rate_min(&self) -> Result<u32> {
            Ok(48000)
        }
        pub fn set_channels(&self, _v: u32) -> Result<()> {
            Ok(())
        }
        pub fn set_rate(&self, _v: u32, _dir: ValueOr) -> Result<()> {
            Ok(())
        }
        pub fn set_format(&self, _v: Format) -> Result<()> {
            Ok(())
        }
        pub fn set_access(&self, _v: Access) -> Result<()> {
            Ok(())
        }
    }

    #[derive(Debug, Clone, Copy)]
    pub enum Format {
        S16LE,
    }

    impl Format {
        pub const fn s16() -> Format {
            Format::S16LE
        }
    }

    #[derive(Debug, Clone, Copy)]
    pub enum Access {
        RWInterleaved,
    }
}
