// SPDX-License-Identifier: MIT
// (C) 2022 The Asahi Linux Contributors
//
// Darwin port (SiliconForest, J293). Changed against upstream types.rs:
// - Elem (an ALSA control element) is gone and Mixer no longer holds elements: it
//   holds the speaker's index in the kernel device (/dev/sfspeaker-safety), found by
//   the speaker's name, which was the prefix of its ALSA control names.
// - Mixer::new(): the VSENSE/ISENSE switches are always on in the kernel (nothing to
//   write); the amplifier gain is fixed by the kernel, so it is not written to its
//   maximum, only read (sfspk_info.amp_gain) where upstream read the control back.
// - Mixer::set_lvl() writes the volume with SFSPK_IOC_SET_VOLUME.
// - `&Ctl` parameters are `&dyn SpeakerDevice`; Ini is the local ini::Ini.
// - Globals: #[allow(dead_code)] for the config keys that have no use on Darwin.
// Globals, SpeakerState, Speaker::new()'s derived limits and start temperatures,
// run_model(), skip_model() and update() are upstream's code, unchanged.

use super::device::{SpeakerDevice, SFSPK_SPEAKER_NAMES};
use super::ini::Ini;
use log::{debug, info};

use super::helpers;

/**
    Mixer struct representing the controls associated with a given
    Speaker.

    drv:    speaker name
    index:  the speaker's place in the kernel device's volume array
*/
struct Mixer {
    drv: String,
    index: usize,
    level_name: String,
    amp_gain_name: String,
}

impl Mixer {
    fn new(name: &str, card: &dyn SpeakerDevice, globals: &Globals) -> Mixer {
        let prefix = if name == "Mono" {
            "".to_string()
        } else {
            name.to_owned() + " "
        };

        /*
         * Upstream locks and reads "<name> VSENSE Switch", "<name> ISENSE Switch",
         * "<name> Speaker Playback Volume" and "<name> Amp Gain Volume" here and
         * panics if one of them does not exist. The device has the four speakers of
         * the J293 and no others; the lock was taken when it was opened.
         */
        let index = SFSPK_SPEAKER_NAMES
            .iter()
            .position(|n| *n == name)
            .unwrap_or_else(|| {
                panic!(
                    "Could not lock elem {}. no such speaker in the device",
                    prefix.clone() + &globals.ctl_volume
                )
            });

        let ret = Mixer {
            drv: name.to_owned(),
            index,
            level_name: prefix.clone() + &globals.ctl_volume,
            amp_gain_name: prefix + &globals.ctl_amp_gain,
        };

        /* Elem::new() reads each element once; this also checks the device state. */
        helpers::read_info(card, &ret.level_name);

        /*
         * Upstream sets the amp gain to the max available here (the kernel clamps
         * it). The Darwin kernel fixes the gain itself and has nothing to set.
         */

        ret
    }

    fn get_amp_gain(&mut self, card: &dyn SpeakerDevice) -> f32 {
        let val: i32 = helpers::read_info(card, &self.amp_gain_name)
            .amp_gain
            .try_into()
            .unwrap_or_else(|_| panic!("Could not read amp gain for {}", self.drv));

        helpers::int_to_db(&helpers::AMP_GAIN_TLV, 0x14, val).to_db()
    }

    fn set_lvl(&mut self, card: &dyn SpeakerDevice, lvl: f32) {
        let new_val: i32 = helpers::db_to_int(card, &self.level_name, lvl);

        helpers::write_volume(card, self.index, new_val, &self.level_name);
    }
}

#[derive(Clone)]
#[allow(dead_code)] // visense_pcm, ctl_vsense, ctl_isense: still parsed (and required), not used
pub struct Globals {
    pub visense_pcm: usize,
    pub channels: usize,
    pub period: usize,
    pub t_ambient: f32,
    pub t_window: f32,
    pub t_hysteresis: f32,
    pub ctl_vsense: String,
    pub ctl_isense: String,
    pub ctl_amp_gain: String,
    pub ctl_volume: String,
    pub uclamp_min: Option<usize>,
    pub uclamp_max: Option<usize>,
}

impl Globals {
    pub fn parse(config: &Ini) -> Self {
        Self {
            visense_pcm: helpers::parse_int(config, "Globals", "visense_pcm"),
            channels: helpers::parse_int(config, "Globals", "channels"),
            period: helpers::parse_int(config, "Globals", "period"),
            t_ambient: helpers::parse_float(config, "Globals", "t_ambient"),
            t_window: helpers::parse_float(config, "Globals", "t_window"),
            t_hysteresis: helpers::parse_float(config, "Globals", "t_hysteresis"),
            ctl_vsense: helpers::parse_string(config, "Controls", "vsense"),
            ctl_isense: helpers::parse_string(config, "Controls", "isense"),
            ctl_amp_gain: helpers::parse_string(config, "Controls", "amp_gain"),
            ctl_volume: helpers::parse_string(config, "Controls", "volume"),
            uclamp_min: helpers::parse_opt_int(config, "Globals", "uclamp_min"),
            uclamp_max: helpers::parse_opt_int(config, "Globals", "uclamp_max"),
        }
    }
}

/**
    Struct representing a driver. Parameters are parsed out of a config
    file, which is loaded at runtime based on the machine's DT compatible
    string.

    name:        driver name as it appears in ALSA
    alsa_iface:  Mixer struct with handles to the driver's control elements
    r_dc:        dc resistance of the voice coil (ohms)
    tau_coil:    voice coil ramp time constant (seconds)
    tau_magnet:  magnet ramp time constant (seconds)
    tr_coil:     thermal resistance of voice coil (*C/W)
    t_limit:  absolute max temp of the voice coil (*C)

    Borrows the handle to the control interface to do calculations.
*/
#[derive(Debug, Default, Copy, Clone)]
pub struct SpeakerState {
    pub t_coil: f64,
    pub t_magnet: f64,

    pub t_coil_hyst: f32,
    pub t_magnet_hyst: f32,

    pub min_gain: f32,
    pub gain: f32,
}

pub struct Speaker {
    pub name: String,
    pub group: usize,
    alsa_iface: Mixer,
    tau_coil: f32,
    tau_magnet: f32,
    tr_coil: f32,
    tr_magnet: f32,
    t_limit: f32,
    t_headroom: f32,
    z_nominal: f32,
    is_scale: f32,
    vs_scale: f32,
    is_chan: usize,
    vs_chan: usize,

    g: Globals,
    pub s: SpeakerState,
}

impl Speaker {
    pub fn new(globals: &Globals, name: &str, config: &Ini, ctl: &dyn SpeakerDevice, cold_boot: bool) -> Speaker {
        info!("Speaker [{}]:", name);

        let section = "Speaker/".to_owned() + name;
        let mut new_speaker: Speaker = Speaker {
            name: name.to_string(),
            alsa_iface: Mixer::new(name, ctl, globals),
            group: helpers::parse_int(config, &section, "group"),
            tau_coil: helpers::parse_float(config, &section, "tau_coil"),
            tau_magnet: helpers::parse_float(config, &section, "tau_magnet"),
            tr_coil: helpers::parse_float(config, &section, "tr_coil"),
            tr_magnet: helpers::parse_float(config, &section, "tr_magnet"),
            t_limit: helpers::parse_float(config, &section, "t_limit"),
            t_headroom: helpers::parse_float(config, &section, "t_headroom"),
            z_nominal: helpers::parse_float(config, &section, "z_nominal"),
            is_scale: helpers::parse_float(config, &section, "is_scale"),
            vs_scale: helpers::parse_float(config, &section, "vs_scale"),
            is_chan: helpers::parse_int(config, &section, "is_chan"),
            vs_chan: helpers::parse_int(config, &section, "vs_chan"),
            g: globals.clone(),
            s: Default::default(),
        };

        let s = &mut new_speaker.s;

        s.t_coil = if cold_boot {
            // Assume warm but not warm enough to limit
            (new_speaker.t_limit - globals.t_window) as f64 - 1f64
        } else {
            // Worst case startup assumption
            new_speaker.t_limit as f64
        };
        s.t_magnet = globals.t_ambient as f64
            + (s.t_coil - globals.t_ambient as f64)
                * (new_speaker.tr_magnet / (new_speaker.tr_magnet + new_speaker.tr_coil)) as f64;

        let max_dt = new_speaker.t_limit - globals.t_ambient;
        let max_pwr = max_dt / (new_speaker.tr_magnet + new_speaker.tr_coil);

        let amp_gain = new_speaker.alsa_iface.get_amp_gain(ctl);

        // Worst-case peak power is 2x RMS power
        let peak_pwr = 10f32.powf(amp_gain / 10.) / new_speaker.z_nominal * 2.;

        s.min_gain = ((max_pwr / peak_pwr).log10() * 10.).min(0.);

        assert!(new_speaker.is_chan < globals.channels);
        assert!(new_speaker.vs_chan < globals.channels);
        assert!(new_speaker.t_limit - globals.t_window > globals.t_ambient);

        info!("  Group: {}", new_speaker.group);
        info!("  Max temperature: {:.1} °C", new_speaker.t_limit);
        info!("  Amp gain: {} dBV", amp_gain);
        info!("  Max power: {:.2} W", max_pwr);
        info!("  Peak power: {} W", peak_pwr);
        info!("  Min gain: {:.2} dB", s.min_gain);

        new_speaker
    }

    pub fn run_model(&mut self, buf: &[i16], sample_rate: f32) -> f32 {
        let s = &mut self.s;

        let step = 1. / sample_rate;
        let alpha_coil = (step / (self.tau_coil + step)) as f64;
        let alpha_magnet = (step / (self.tau_magnet + step)) as f64;

        let mut pwr_sum = 0f32;

        for sample in buf.chunks(self.g.channels) {
            assert!(sample.len() == self.g.channels);

            let v = sample[self.vs_chan] as f32 / 32768.0 * self.vs_scale;
            let i = sample[self.is_chan] as f32 / 32768.0 * self.is_scale;
            let p = v * i;

            let t_coil_target = s.t_magnet + (p * self.tr_coil) as f64;
            let t_magnet_target = (self.g.t_ambient + p * self.tr_magnet) as f64;

            s.t_coil = t_coil_target * alpha_coil + s.t_coil * (1. - alpha_coil);
            s.t_magnet = t_magnet_target * alpha_magnet + s.t_magnet * (1. - alpha_magnet);

            if s.t_coil > (self.t_limit + self.t_headroom) as f64 {
                panic!(
                    "{}: Coil temperature limit exceeded ({} > {})",
                    self.name, s.t_coil, self.t_limit
                );
            }
            if s.t_magnet > (self.t_limit + self.t_headroom) as f64 {
                panic!(
                    "{}: Magnet temperature limit exceeded ({} > {})",
                    self.name, s.t_magnet, self.t_limit
                );
            }

            pwr_sum += p;
        }

        let pwr_avg: f32 = pwr_sum / ((buf.len() / self.g.channels) as f32);
        /*
         * This really shouldn't happen other than rounding error,
         * if it does there's probably something wrong with the ivsense
         * data.
         */
        if pwr_avg < -0.01 {
            panic!(
                "{}: Negative power, bad ivsense data? ({})",
                self.name, pwr_avg
            );
        }
        let pwr_avg = pwr_avg.max(0.0);

        s.t_coil_hyst = s
            .t_coil_hyst
            .max(s.t_coil as f32)
            .min(s.t_coil as f32 + self.g.t_hysteresis);
        s.t_magnet_hyst = s
            .t_magnet_hyst
            .max(s.t_magnet as f32)
            .min(s.t_magnet as f32 + self.g.t_hysteresis);

        let temp = s.t_coil_hyst.max(s.t_magnet_hyst);

        let reduction = (temp - (self.t_limit - self.g.t_window)) / self.g.t_window;
        let gain = s.min_gain * reduction.max(0.);

        s.gain = gain;

        if s.gain > -0.01 {
            s.gain = 0.;
        }

        debug!(
            "{:>15}: Coil {:>6.2} °C Magnet {:>6.2} °C Power {:>5.2} W Gain {:>6.2} dB",
            self.name, s.t_coil, s.t_magnet, pwr_avg, s.gain
        );

        s.gain
    }

    pub fn skip_model(&mut self, time: f64) {
        let s = &mut self.s;
        let t_coil = s.t_coil - self.g.t_ambient as f64;
        let t_magnet = s.t_magnet - self.g.t_ambient as f64;

        let eta = 1f64 / (1f64 - (self.tau_coil / self.tau_magnet) as f64);
        let a = (-time / self.tau_coil as f64).exp() * (t_coil - eta * t_magnet);
        let b = (-time / self.tau_magnet as f64).exp() * t_magnet;

        s.t_coil = self.g.t_ambient as f64 + a + b * eta;
        s.t_magnet = self.g.t_ambient as f64 + b;

        debug!(
            "{}: SKIP: Coil {:.2} °C Magnet {:.2} °C ({:.2} seconds)",
            self.name, s.t_coil, s.t_magnet, time
        );
    }

    pub fn update(&mut self, ctl: &dyn SpeakerDevice, gain: f32) {
        self.alsa_iface.set_lvl(ctl, gain);
    }
}
