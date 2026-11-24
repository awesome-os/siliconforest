// SPDX-License-Identifier: MIT
// Synthetic sense data for the host tests, shared by the port's tests and the
// upstream reference harness (tests/upstream-ref) so that both see the same samples.
// In-phase sines (a resistive load) with the J293 scales of conf/apple/j293.conf.
#![allow(dead_code)]

pub const RATE: u32 = 48000;
pub const PERIOD: usize = 4096;
pub const CHANNELS: usize = 8;
pub const SPEAKERS: usize = 4;
const IS_SCALE: f64 = 3.75;
const VS_SCALE: f64 = 14.0;
const TONE_HZ: f64 = 1000.0;

/// (periods, peak volts, peak amperes) of speaker 0; speaker n gets (1 - n/10) of both.
pub const PHASES: [(usize, f64, f64); 4] = [
    /* 10 s of 2 V / 0.2 A: 0.2 W, the coil cools from its start temperature */
    (118, 2.0, 0.2),
    /* 60 s of 1 W: the coil goes past 110 C and the gain has to come down */
    (704, 4.4721, 0.44721),
    /* 30 s of silence: cools, the gain comes back with hysteresis */
    (352, 0.0, 0.0),
    /* 5 s of 2 V / 0.2 A again */
    (59, 2.0, 0.2),
];

pub fn total_periods() -> usize {
    PHASES.iter().map(|p| p.0).sum()
}

fn level(period: usize) -> (f64, f64) {
    let mut start = 0;
    for (len, v, i) in PHASES {
        if period < start + len {
            return (v, i);
        }
        start += len;
    }
    (0.0, 0.0)
}

fn sample(val: f64, scale: f64) -> i16 {
    (val / scale * 32768.0).round().clamp(-32768.0, 32767.0) as i16
}

/// One period (PERIOD frames of CHANNELS samples: current, voltage per speaker)
pub fn period(n: usize) -> Vec<i16> {
    tone(n, level(n))
}

pub fn tone(n: usize, (v_peak, i_peak): (f64, f64)) -> Vec<i16> {
    let mut buf = Vec::with_capacity(PERIOD * CHANNELS);
    for frame in 0..PERIOD {
        let t = (n * PERIOD + frame) as f64 / RATE as f64;
        let s = (2.0 * std::f64::consts::PI * TONE_HZ * t).sin();
        for spk in 0..SPEAKERS {
            let k = 1.0 - spk as f64 / 10.0;
            buf.push(sample(i_peak * k * s, IS_SCALE));
            buf.push(sample(v_peak * k * s, VS_SCALE));
        }
    }
    buf
}
