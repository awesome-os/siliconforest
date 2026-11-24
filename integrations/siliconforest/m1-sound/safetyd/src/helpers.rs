// SPDX-License-Identifier: MIT
// (C) 2022 The Asahi Linux Contributors
//
// Darwin port (SiliconForest, J293). Changed against upstream helpers.rs:
// - The alsa crate is gone. open_card() opens /dev/sfspeaker-safety; the ALSA element
//   wrappers (new_elemvalue, read_ev, write_ev, lock_el, get_range_db) are replaced by
//   read_info(), write_unlock() and write_volume() on the device, which panic on any
//   error exactly as the wrappers did; open_pcm() is replaced by read_sense().
// - int_to_db()/db_to_int() no longer ask alsa-lib: the two dB scales of the amplifier
//   driver (tas2770.c) are constants here and snd_tlv_convert_{to,from}_dB() for a
//   dB scale is transcribed below (tlv_convert_to_db/tlv_convert_from_db), with the
//   element range alsa-lib would have read from the kernel (0..sfspk_info.volume_max).
// - A device that reports tripped != 0 (or a version this code does not know) is a
//   fatal error, like a failing ALSA call.
// - The config type is the local ini::Ini (configparser is not available offline).
// The parse_* functions are unchanged.

use std::io;

use super::device::{
    SfSpeaker, PdspkInfo, PdspkVolume, SpeakerDevice, SFSPK_SENSE_CHANNELS, SFSPK_VERSION,
};
use super::ini::Ini;

/// alsa::mixer::MilliBel, the two methods the daemon uses.
#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord)]
pub struct MilliBel(pub i64);

impl MilliBel {
    pub fn to_db(self) -> f32 {
        (self.0 as f32) / 100.0
    }
}

/// alsa::Round, passed to alsa-lib as its `xdir`. Note what alsa-lib (1.2.x) makes of
/// it: only xdir > 0 rounds up and only xdir < 0 rounds down, 0 rounds to the nearest
/// step. So the `Round::Floor` that upstream passes for the volume gives the NEAREST
/// 0.5 dB step (ties upwards), not the next lower one, and so does this port.
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum Round {
    Floor = 0,
    #[allow(dead_code)]
    Ceil = 1,
}

/// DECLARE_TLV_DB_SCALE(name, min, step, mute), in 0.01 dB
#[derive(Debug, Copy, Clone)]
pub struct DbScale {
    pub min: i64,
    pub step: i64,
    pub mute: bool,
}

/// tas2770_playback_volume: "Speaker Playback Volume", dB = -100.5 + 0.5 * value, 0..201
pub const VOLUME_TLV: DbScale = DbScale {
    min: -10050,
    step: 50,
    mute: false,
};
/// tas2770_digital_tlv: "Amp Gain Volume", dBV = 11 + 0.5 * value
pub const AMP_GAIN_TLV: DbScale = DbScale {
    min: 1100,
    step: 50,
    mute: false,
};

const SND_CTL_TLV_DB_GAIN_MUTE: i64 = -9999999;

/// snd_tlv_convert_to_dB() of alsa-lib (src/control/tlv.c), SND_CTL_TLVT_DB_SCALE case
pub fn tlv_convert_to_db(tlv: &DbScale, rangemin: i64, _rangemax: i64, volume: i64) -> MilliBel {
    if tlv.mute && volume <= rangemin {
        MilliBel(SND_CTL_TLV_DB_GAIN_MUTE)
    } else {
        MilliBel((volume - rangemin) * tlv.step + tlv.min)
    }
}

/// snd_tlv_convert_from_dB() of alsa-lib (src/control/tlv.c), SND_CTL_TLVT_DB_SCALE case
pub fn tlv_convert_from_db(
    tlv: &DbScale,
    rangemin: i64,
    rangemax: i64,
    db_gain: MilliBel,
    dir: Round,
) -> i64 {
    let xdir = dir as i64;
    let db_gain = db_gain.0;
    let min = tlv.min;
    let max = min + tlv.step * (rangemax - rangemin);

    if db_gain <= min {
        if db_gain > SND_CTL_TLV_DB_GAIN_MUTE && xdir > 0 && tlv.mute {
            rangemin + 1
        } else {
            rangemin
        }
    } else if db_gain >= max {
        rangemax
    } else {
        let mut v = (db_gain - min) * (rangemax - rangemin);
        if xdir > 0 {
            v += (max - min) - 1;
        } else if xdir == 0 {
            v += ((max - min) + 1) / 2;
        }
        v / (max - min) + rangemin
    }
}

/// Was: open the ALSA control device. Opening the device also takes the lock on the
/// unlock control and the volumes (the kernel allows one opener).
pub fn open_card(card: &str) -> SfSpeaker {
    let ctldev: SfSpeaker = match SfSpeaker::open(card) {
        Ok(ctldev) => ctldev,
        Err(e) => {
            panic!("{}: Could not open speaker safety device! Error: {}", card, e);
        }
    };

    ctldev
}

/// Was: open_pcm(). There is no separate PCM to open, the sense stream is read() from
/// the same device; what is left of the hw_params is the check of the channel count.
pub fn check_pcm(chans: usize) {
    if chans != SFSPK_SENSE_CHANNELS {
        panic!(
            "Sense stream has {} channels, the config wants {}",
            SFSPK_SENSE_CHANNELS, chans
        );
    }
}

/**
    Was: io.readi(). Blocking read of buf.len() / chans frames of S16LE sense data,
    returns the number of frames. Like a blocking snd_pcm_readi() it only returns
    short on an error, which is passed on (EINTR included).
*/
pub fn read_sense(
    card: &dyn SpeakerDevice,
    bytes: &mut Vec<u8>,
    buf: &mut [i16],
    chans: usize,
) -> io::Result<usize> {
    bytes.resize(buf.len() * 2, 0);

    let mut filled = 0;
    while filled < bytes.len() {
        let n = card.read(&mut bytes[filled..])?;
        if n == 0 {
            return Err(io::Error::new(
                io::ErrorKind::UnexpectedEof,
                "end of sense stream",
            ));
        }
        filled += n;
    }

    for (sample, raw) in buf.iter_mut().zip(bytes.chunks_exact(2)) {
        *sample = i16::from_le_bytes([raw[0], raw[1]]);
    }

    Ok(buf.len() / chans)
}

/**
    Wrapper around configparser::ini::Ini.getint()
    to safely unwrap the Result<Option<i64>, E> returned by
    it.
*/
pub fn parse_int<T: TryFrom<i64>>(config: &Ini, section: &str, key: &str) -> T
where
    <T as TryFrom<i64>>::Error: std::fmt::Debug,
{
    config
        .getint(section, key)
        .unwrap_or_else(|_| panic!("{}/{}: Invalid value", section, key))
        .unwrap_or_else(|| panic!("{}/{}: Missing key", section, key))
        .try_into()
        .expect("{}/{}: Out of bounds")
}

pub fn parse_opt_int<T: TryFrom<i64>>(config: &Ini, section: &str, key: &str) -> Option<T>
where
    <T as TryFrom<i64>>::Error: std::fmt::Debug,
{
    config
        .getint(section, key)
        .unwrap_or_else(|_| panic!("{}/{}: Invalid value", section, key))
        .map(|a| a.try_into().expect("{}/{}: Out of bounds"))
}

/**
    Wrapper around configparser::ini::Ini.getfloat()
    to safely unwrap the Result<Option<f64>, E> returned by
    it.
*/
pub fn parse_float(config: &Ini, section: &str, key: &str) -> f32 {
    let val = config
        .getfloat(section, key)
        .unwrap_or_else(|_| panic!("{}/{}: Invalid value", section, key))
        .unwrap_or_else(|| panic!("{}/{}: Missing key", section, key)) as f32;

    assert!(val.is_finite());
    val
}

/**
    Wrapper around configparser::ini::Ini.getfloat()
    to safely unwrap the Result<Option<f64>, E> returned by
    it.
*/
pub fn parse_string(config: &Ini, section: &str, key: &str) -> String {
    config
        .get(section, key)
        .unwrap_or_else(|| panic!("{}/{}: Missing key", section, key))
}

/**
    Was: read_ev(), the wrapper for alsa::ctl::Ctl::elem_read(). Every control value
    the daemon reads comes out of SFSPK_IOC_INFO. `name` is the ALSA control the
    caller is reading, for the message.
*/
pub fn read_info(card: &dyn SpeakerDevice, name: &str) -> PdspkInfo {
    let info = match card.info() {
        Ok(val) => val,
        Err(e) => {
            panic!("Could not read elem value {}. device error: {:?}", name, e);
        }
    };

    if info.version != SFSPK_VERSION {
        panic!(
            "Could not read elem value {}. unknown device version {}",
            name, info.version
        );
    }
    if info.tripped != 0 {
        panic!(
            "Could not read elem value {}. the kernel shut the amplifiers down (tripped = {})",
            name, info.tripped
        );
    }

    info
}

/**
    Was: write_ev() of the "Speaker Volume Unlock" element. Any error, ETIMEDOUT
    (the lease ran out) included, is fatal.
*/
pub fn write_unlock(card: &dyn SpeakerDevice, magic: i32, name: &str) {
    match card.ping(magic as u32) {
        Ok(val) => val,
        Err(e) => {
            panic!("Could not write elem value {}. device error: {:?}", name, e);
        }
    };
}

/**
    Was: write_ev() of one speaker's volume element. The device takes all four
    volumes in one call; the other three are passed back as the kernel reports them.
*/
pub fn write_volume(card: &dyn SpeakerDevice, index: usize, val: i32, name: &str) {
    let mut volume = PdspkVolume {
        volume: read_info(card, name).volume,
    };
    volume.volume[index] = val.try_into().unwrap();

    match card.set_volume(&volume) {
        Ok(val) => val,
        Err(e) => {
            panic!("Could not write elem value {}. device error: {:?}", name, e);
        }
    };
}

pub fn int_to_db(tlv: &DbScale, rangemax: i64, val: i32) -> MilliBel {
    tlv_convert_to_db(tlv, 0, rangemax, val.into())
}

/**
    Was: convert_from_db() of the volume element. alsa-lib reads the element's range
    for every conversion, and macaudio reports the current volume limit as its
    maximum; that is sfspk_info.volume_max here.
*/
pub fn db_to_int(card: &dyn SpeakerDevice, name: &str, val: f32) -> i32 {
    let mb: MilliBel = MilliBel((val * 100.0) as i64);
    let rangemax = read_info(card, name).volume_max;

    tlv_convert_from_db(&VOLUME_TLV, 0, rangemax.into(), mb, Round::Floor) as i32
}
