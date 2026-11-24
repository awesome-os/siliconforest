// SPDX-License-Identifier: MIT
// Host reference harness: runs UNMODIFIED upstream types.rs / helpers.rs
// (../upstream, copies of speakersafetyd 4175df66) over the synthetic sense data of
// tests/common/synth.rs and prints what the port's tests compare against.
//
// usage: upstream-ref <config file> cold|warm skip|noskip
//   (skip: one skip_model(12.5) in the middle of the silent phase)
// output lines:
//   I ...   startup numbers of each speaker
//   M ...   per period: group gain and each speaker's state, floats as bit patterns
//   T ...   the control writes in order: "read" (start of a period), "vol a,b,c,d",
//           "ping"
//   D ...   dB -> control value table of the volume control, from alsa-lib
//
// The loop below is upstream main.rs's (speaker setup, initial unlock, cold/warm
// preset, model, update, unlock), without the PCM, the wall clock and the logging.

use std::collections::BTreeMap;

use configparser::ini::Ini;

#[path = "../upstream/helpers.rs"]
#[allow(dead_code)]
mod helpers;
#[path = "../../common/synth.rs"]
mod synth;
#[path = "../upstream/types.rs"]
#[allow(dead_code)]
mod types;

const UNLOCK_MAGIC: i32 = 0xdec1be15u32 as i32;

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

fn get_speakers(config: &Ini) -> Vec<String> {
    config
        .sections()
        .iter()
        .filter_map(|a| a.strip_prefix("Speaker/"))
        .map(|a| a.to_string())
        .collect()
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let cold_boot = match args[2].as_str() {
        "cold" => true,
        "warm" => false,
        _ => panic!("cold or warm"),
    };

    let skip = match args[3].as_str() {
        "skip" => true,
        "noskip" => false,
        _ => panic!("skip or noskip"),
    };

    let mut cfg: Ini = Ini::new_cs();
    cfg.load(&args[1]).expect("Failed to read config file");
    let globals = types::Globals::parse(&cfg);
    assert!(globals.period == synth::PERIOD && globals.channels == synth::CHANNELS);

    let ctl: alsa::ctl::Ctl = helpers::open_card("hw:AppleJ293");
    let mut out: Vec<String> = Vec::new();
    let mut seen = 0;
    let mut flush = |out: &mut Vec<String>| {
        let trace = ctl.trace.borrow();
        for t in &trace[seen..] {
            out.push(format!("T {}", t));
        }
        seen = trace.len();
    };

    let mut groups: BTreeMap<usize, SpeakerGroup> = BTreeMap::new();
    for i in get_speakers(&cfg) {
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

    let mut sample_rate_elem = types::Elem::new(
        "Speaker Sample Rate".to_string(),
        &ctl,
        alsa::ctl::ElemType::Integer,
    );
    let sample_rate = sample_rate_elem.read_int(&ctl);
    assert!(sample_rate == synth::RATE as i32);

    let mut unlock_elem = types::Elem::new(
        "Speaker Volume Unlock".to_string(),
        &ctl,
        alsa::ctl::ElemType::Integer,
    );

    unlock_elem.write_int(&ctl, UNLOCK_MAGIC);

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

        /* halfway through the silence: the catch-up for a 12.5 s gap */
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

        unlock_elem.write_int(&ctl, UNLOCK_MAGIC);
        flush(&mut out);
    }

    /* dB -> control value, every 0.01 dB from -101 dB to +1 dB, unlocked and locked */
    for (label, rangemax) in [("201", 201), ("1", 1)] {
        for mb in -10100..=100i64 {
            let v = ctl.raw_convert_from_db(
                -10050,
                50,
                0,
                rangemax,
                alsa::mixer::MilliBel(mb),
                alsa::Round::Floor,
            );
            out.push(format!("D {} {} {}", label, mb, v));
        }
    }

    println!("{}", out.join("\n"));
}
