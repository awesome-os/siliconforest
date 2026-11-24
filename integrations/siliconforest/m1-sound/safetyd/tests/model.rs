// SPDX-License-Identifier: MIT
// Host tests of the port's model and conversions (cargo test, x86_64 Linux), against
// the upstream reference harness in tests/upstream-ref (unmodified upstream types.rs
// and helpers.rs, dB conversions by the real alsa-lib). Run with run-host-tests.sh,
// which builds the harness and sets SPEAKERSAFETYD_UPSTREAM_REF.

#[path = "../src/main.rs"]
#[allow(dead_code)]
mod daemon;
#[path = "common/fake.rs"]
mod fake;
#[path = "common/synth.rs"]
mod synth;

use std::collections::BTreeMap;

use daemon::device::{self, SpeakerDevice};
use daemon::helpers::{self, MilliBel, Round};
use daemon::ini::Ini;
use daemon::types;

const CONF: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/conf/apple/j293.conf");
const UNLOCK_MAGIC: i32 = 0xdec1be15u32 as i32;

fn silent() -> fake::Script {
    fake::Script::new(Box::new(|_| None))
}

struct SpeakerGroup {
    speakers: Vec<types::Speaker>,
    gain: f32,
}

impl Default for SpeakerGroup {
    fn default() -> Self {
        Self {
            speakers: Default::default(),
            gain: f32::NAN,
        }
    }
}

/// The same run as tests/upstream-ref/src/main.rs, with the port's types.rs/helpers.rs
/// and the fake device; returns the I, M and T lines.
fn port_run(cold_boot: bool, skip: bool) -> Vec<String> {
    let mut cfg: Ini = Ini::new_cs();
    cfg.load(CONF).expect("Failed to read config file");
    let globals = types::Globals::parse(&cfg);
    assert!(globals.period == synth::PERIOD && globals.channels == synth::CHANNELS);

    let ctl = fake::FakeDevice::new(silent(), None);
    let mut out: Vec<String> = Vec::new();
    let mut seen = 0;
    let mut flush = |out: &mut Vec<String>| {
        let trace = fake::normalize(&ctl.trace());
        out.extend_from_slice(&trace[seen..]);
        seen = trace.len();
    };

    let mut groups: BTreeMap<usize, SpeakerGroup> = BTreeMap::new();
    let names: Vec<String> = cfg
        .sections()
        .iter()
        .filter_map(|a| a.strip_prefix("Speaker/"))
        .map(|a| a.to_string())
        .collect();
    for i in names {
        let speaker: types::Speaker = types::Speaker::new(&globals, &i, &cfg, &ctl, cold_boot);
        out.push(format!(
            "I {} group {} t_coil {:016x} t_magnet {:016x} min_gain {:08x}",
            speaker.name,
            speaker.group,
            speaker.s.t_coil.to_bits(),
            speaker.s.t_magnet.to_bits(),
            speaker.s.min_gain.to_bits()
        ));
        groups.entry(speaker.group).or_default().speakers.push(speaker);
    }

    let sample_rate = helpers::read_info(&ctl, "Speaker Sample Rate").sample_rate as i32;
    assert!(sample_rate == synth::RATE as i32);

    helpers::write_unlock(&ctl, UNLOCK_MAGIC, "Speaker Volume Unlock");

    for (_idx, group) in groups.iter_mut() {
        if cold_boot {
            group.speakers.iter_mut().for_each(|s| s.update(&ctl, 0.0));
            group.gain = 0.0;
        } else {
            group.gain = -999.0;
        }
    }
    flush(&mut out);

    for n in 0..synth::total_periods() {
        let buf = synth::period(n);
        let buf_read = &buf[..];
        out.push("T read".to_string());

        if skip && n == 118 + 704 + 176 {
            for (_, group) in groups.iter_mut() {
                group.speakers.iter_mut().for_each(|s| s.skip_model(12.5));
            }
        }

        for (idx, group) in groups.iter_mut() {
            let gain = group
                .speakers
                .iter_mut()
                .map(|s| s.run_model(buf_read, sample_rate as f32))
                .reduce(f32::min)
                .unwrap();
            if gain != group.gain {
                group.speakers.iter_mut().for_each(|s| s.update(&ctl, gain));
                group.gain = gain;
            }
            let states: Vec<String> = group
                .speakers
                .iter()
                .map(|s| {
                    format!(
                        "{:016x}/{:016x}/{:08x}/{:08x}/{:08x}",
                        s.s.t_coil.to_bits(),
                        s.s.t_magnet.to_bits(),
                        s.s.t_coil_hyst.to_bits(),
                        s.s.t_magnet_hyst.to_bits(),
                        s.s.gain.to_bits()
                    )
                })
                .collect();
            out.push(format!("M {} g{} {:08x} {}", n, idx, gain.to_bits(), states.join(" ")));
        }

        helpers::write_unlock(&ctl, UNLOCK_MAGIC, "Speaker Volume Unlock");
        flush(&mut out);
    }

    out
}

fn compare(mode: &str, skip: &str) {
    let reference: Vec<String> = fake::upstream_ref(CONF, mode, skip)
        .into_iter()
        .filter(|l| !l.starts_with("D "))
        .collect();
    let port = port_run(mode == "cold", skip == "skip");

    let count = |v: &[String], p: &str| v.iter().filter(|l| l.starts_with(p)).count();
    assert_eq!(count(&reference, "M "), synth::total_periods());
    for (i, (r, p)) in reference.iter().zip(port.iter()).enumerate() {
        assert_eq!(r, p, "{} {}: line {} differs from upstream", mode, skip, i);
    }
    assert_eq!(reference.len(), port.len());

    /* say what was compared (cargo test -- --nocapture) */
    let gain = |l: &String| f32::from_bits(u32::from_str_radix(l.split(' ').nth(3).unwrap(), 16).unwrap());
    let gains: Vec<f32> = port.iter().filter(|l| l.starts_with("M ")).map(gain).collect();
    let min = gains.iter().cloned().fold(0f32, f32::min);
    let limited = gains.iter().filter(|g| **g != 0.0).count();
    let vols: Vec<&String> = port.iter().filter(|l| l.starts_with("T vol")).collect();
    println!(
        "{} {}: {} lines identical to upstream ({} periods, {} limited, lowest gain {:.4} dB at period {}, {} volume writes, lowest {})",
        mode,
        skip,
        port.len(),
        gains.len(),
        limited,
        min,
        gains.iter().position(|g| *g == min).unwrap(),
        vols.len(),
        vols.iter().map(|l| l[6..].to_string()).min_by_key(|v| v.split(',').next().unwrap().parse::<u32>().unwrap()).unwrap(),
    );
}

#[test]
fn trajectory_matches_upstream_cold() {
    compare("cold", "noskip");
}

#[test]
fn trajectory_matches_upstream_cold_with_skip() {
    compare("cold", "skip");
}

#[test]
fn trajectory_matches_upstream_warm() {
    compare("warm", "noskip");
}

#[test]
fn trajectory_matches_upstream_warm_with_skip() {
    compare("warm", "skip");
}

/// The numbers the real daemon logs for this machine under Linux.
#[test]
fn j293_start_numbers() {
    let mut cfg: Ini = Ini::new_cs();
    cfg.load(CONF).unwrap();
    let globals = types::Globals::parse(&cfg);
    let ctl = fake::FakeDevice::new(silent(), None);

    for (cold, t_coil) in [(true, 109.0f64), (false, 130.0f64)] {
        for name in device::SFSPK_SPEAKER_NAMES {
            let s = types::Speaker::new(&globals, name, &cfg, &ctl, cold);
            assert_eq!(s.s.t_coil, t_coil);
            assert_eq!(format!("{:.2}", s.s.min_gain), "-12.03");
            assert_eq!(s.group, 0);
        }
    }

    /* the amp gain as the port reads it, and the two powers by the logged formulas */
    let amp_gain = helpers::int_to_db(&helpers::AMP_GAIN_TLV, 0x14, ctl.info().unwrap().amp_gain as i32).to_db();
    assert_eq!(format!("{}", amp_gain), "18.5");
    let max_pwr = (130.0f32 - 50.0) / (49.10f32 + 38.30);
    let peak_pwr = 10f32.powf(amp_gain / 10.) / 9.70f32 * 2.;
    assert_eq!(format!("{:.2}", max_pwr), "0.92");
    assert_eq!(format!("{}", peak_pwr), "14.596821");
    assert_eq!(format!("{:.2}", ((max_pwr / peak_pwr).log10() * 10.).min(0.)), "-12.03");
    println!("amp gain {} dBV, max power {:.2} W, peak power {} W, min gain {:.2} dB, cold start 109 C, warm start 130 C",
        amp_gain, max_pwr, peak_pwr, ((max_pwr / peak_pwr).log10() * 10.).min(0.));
}

/// dB -> control value: the port's transcription against alsa-lib itself, for every
/// 0.01 dB from -101 dB to +1 dB, with the range unlocked (0..201) and locked (0..1).
#[test]
fn db_to_control_value_matches_alsa_lib() {
    let reference = fake::upstream_ref(CONF, "cold", "noskip");
    let mut checked = 0;
    for line in reference.iter().filter(|l| l.starts_with("D ")) {
        let f: Vec<i64> = line[2..].split(' ').map(|v| v.parse().unwrap()).collect();
        let (rangemax, mb, want) = (f[0], f[1], f[2]);
        let got = helpers::tlv_convert_from_db(&helpers::VOLUME_TLV, 0, rangemax, MilliBel(mb), Round::Floor);
        assert_eq!(got, want, "{} mB, range 0..{}", mb, rangemax);
        checked += 1;
    }
    assert_eq!(checked, 2 * 10201);
    println!("{} conversions identical to alsa-lib", checked);
}

#[test]
fn db_to_control_value_spot_checks() {
    let ctl = fake::FakeDevice::new(silent(), None);

    /* locked: the range is 0..1 */
    assert_eq!(helpers::db_to_int(&ctl, "x", 0.0), 1);
    assert_eq!(helpers::db_to_int(&ctl, "x", -12.03), 1);
    assert_eq!(helpers::db_to_int(&ctl, "x", -100.0), 1);
    assert_eq!(helpers::db_to_int(&ctl, "x", -100.5), 0);

    ctl.ping(fake::MAGIC).unwrap();
    let table = [
        (0.0f32, 201),
        (3.0, 201),
        (-0.01, 201),
        (-0.24, 201),
        (-0.25, 201), /* a tie goes up */
        (-0.26, 200),
        (-0.5, 200),
        (-6.0, 189),
        (-12.0, 177),
        (-12.03, 177), /* j293 min gain: 177 = -12.0 dB */
        (-12.0267972946167, 177),
        (-12.25, 177),
        (-12.26, 176),
        (-12.5, 176),
        (-99.9, 1),
        (-100.0, 1),
        (-100.25, 1),
        (-100.26, 0),
        (-100.5, 0),
        (-999.0, 0),
    ];
    for (db, want) in table {
        assert_eq!(helpers::db_to_int(&ctl, "x", db), want, "{} dB", db);
        /* and by the scale's definition dB = -100.5 + 0.5 * value, nearest step */
        let mb = (db * 100.0) as i64;
        let nearest = (((mb + 10050) as f64 / 50.0) + 0.5).floor().clamp(0.0, 201.0) as i32;
        assert_eq!(want, nearest, "{} dB", db);
    }
    println!("0 dB -> {}, -12.03 dB -> {}, -100 dB -> {}",
        helpers::db_to_int(&ctl, "x", 0.0), helpers::db_to_int(&ctl, "x", -12.03), helpers::db_to_int(&ctl, "x", -100.0));
}

/// The volume write keeps the other speakers as the kernel reports them and cannot
/// go above what the kernel allows.
#[test]
fn volume_write_and_lock() {
    let ctl = fake::FakeDevice::new(silent(), None);
    let mut cfg: Ini = Ini::new_cs();
    cfg.load(CONF).unwrap();
    let globals = types::Globals::parse(&cfg);
    let mut s = types::Speaker::new(&globals, "Left Rear", &cfg, &ctl, true);

    /* no ping yet: 0 dB comes out as the locked value */
    s.update(&ctl, 0.0);
    assert_eq!(ctl.info().unwrap().volume, [1, 1, 1, 1]);

    ctl.ping(fake::MAGIC).unwrap();
    s.update(&ctl, 0.0);
    assert_eq!(ctl.info().unwrap().volume, [1, 1, 201, 1]);
    s.update(&ctl, -6.0);
    assert_eq!(ctl.info().unwrap().volume, [1, 1, 189, 1]);
}

#[test]
#[should_panic(expected = "no such speaker")]
fn unknown_speaker_is_fatal() {
    let ctl = fake::FakeDevice::new(silent(), None);
    let mut cfg: Ini = Ini::new_cs();
    cfg.read("[Globals]\nvisense_pcm=2\nt_ambient=50\nt_hysteresis=5\nt_window=20\nchannels=8\nperiod=4096\n[Controls]\nvsense=a\nisense=b\namp_gain=c\nvolume=d\n").unwrap();
    let globals = types::Globals::parse(&cfg);
    types::Speaker::new(&globals, "Centre", &cfg, &ctl, true);
}

/// The request numbers and layouts of sf_speaker_safety.h (Darwin ioctl encoding).
#[test]
fn device_abi() {
    assert_eq!(std::mem::size_of::<device::PdspkInfo>(), 72);
    assert_eq!(std::mem::size_of::<device::PdspkVolume>(), 16);
    assert_eq!(device::SFSPK_IOC_INFO, 0x40485301);
    assert_eq!(device::SFSPK_IOC_PING, 0x80045302);
    assert_eq!(device::SFSPK_IOC_SET_VOLUME, 0x80105303);
}

#[test]
fn config_parser() {
    let mut cfg: Ini = Ini::new_cs();
    cfg.load(CONF).unwrap();
    assert_eq!(
        cfg.sections(),
        ["Globals", "Controls", "Speaker/Left Front", "Speaker/Right Front", "Speaker/Left Rear", "Speaker/Right Rear"]
    );
    assert_eq!(cfg.get("Controls", "volume").unwrap(), "Speaker Playback Volume");
    assert_eq!(cfg.getint("Globals", "period"), Ok(Some(4096)));
    assert_eq!(cfg.getint("Globals", "uclamp_min"), Ok(None));
    assert_eq!(cfg.getfloat("Speaker/Right Rear", "tr_coil"), Ok(Some(38.30)));
    assert_eq!(cfg.getint("Speaker/Right Rear", "vs_chan"), Ok(Some(7)));
    assert!(cfg.getint("Globals", "t_ambient").is_err());
    assert_eq!(cfg.get("globals", "period"), None);

    cfg.read("top = 1\n[A]\n# c\nk : v w ; trailing\nk2\nk = last\n[B]\n").unwrap();
    assert_eq!(cfg.sections(), ["A", "B"]);
    assert_eq!(cfg.get("default", "top").unwrap(), "1");
    assert_eq!(cfg.get("A", "k").unwrap(), "last");
    assert_eq!(cfg.get("A", "k2"), None);
}
