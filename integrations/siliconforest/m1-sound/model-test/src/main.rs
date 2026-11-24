// SPDX-License-Identifier: MIT
// Host harness: is the kernel's speaker model (../kernel/sf_speaker_model.c) ever less
// conservative than the reference?
//
// Reference: upstream speakersafetyd 2.0.1's model code, UNMODIFIED (types.rs and
// helpers.rs of ../safetyd/tests/upstream-ref/upstream, dB conversions by the real
// alsa-lib through that directory's stand-in alsa crate), with j293.conf, cold start,
// amp gain 18.5 dBV, driven with the daemon's loop semantics: 4096-frame periods, the
// group's minimum gain, Speaker::update() -> control value, skip_model(dt - period)
// when more than 4 periods of wall time passed between two period ends.
//
// Kernel: sf_speaker_model.c as a C library, driven as the kernel drives it: run() in
// 1024-frame calls while the stream plays (a shorter call for what is left when the
// stream stops), idle() in 100 ms slices while it does not, volume = 201 - limit_steps
// after each run().
//
// Both get the same frames and the same pauses. See README for the criteria.

use std::collections::BTreeMap;
use std::ffi::c_void;
use std::fmt::Write as _;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Mutex;

use configparser::ini::Ini;

#[path = "../../safetyd/tests/upstream-ref/upstream/helpers.rs"]
#[allow(dead_code)]
mod helpers;
#[path = "../../safetyd/tests/upstream-ref/upstream/types.rs"]
#[allow(dead_code)]
mod types;

const CONF: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../safetyd/conf/apple/j293.conf");
const UNLOCK_MAGIC: i32 = 0xdec1be15u32 as i32;

const CH: usize = 8;
const SPK: usize = 4;
const PERIOD: usize = 4096;
const KCALL: usize = 1024;
const Z_NOMINAL: f64 = 9.7;
const VS_SCALE: f64 = 14.0;
const IS_SCALE: f64 = 3.75;
const NAMES: [&str; SPK] = ["LF", "RF", "LR", "RR"];

// ---------------------------------------------------------------- the kernel model

extern "C" {
    fn sf_spk_model_init(m: *mut c_void, rate: u32, amp_gain_reg: u32) -> i32;
    fn sf_spk_model_set_rate(m: *mut c_void, rate: u32) -> i32;
    fn sf_spk_model_run(m: *mut c_void, sense: *const i16, frames: u32) -> i32;
    fn sf_spk_model_idle(m: *mut c_void, ns: u64);
    fn sf_spk_model_limit_steps(m: *const c_void) -> u32;
    fn sfshim_size() -> usize;
    fn sfshim_fault_speaker(m: *const c_void) -> u32;
    fn sfshim_coil(m: *const c_void, i: u32) -> i64;
    fn sfshim_magnet(m: *const c_void, i: u32) -> i64;
    fn sfshim_alpha_coil(m: *const c_void) -> i64;
    fn sfshim_alpha_magnet(m: *const c_void) -> i64;
}

const Q40: f64 = (1u64 << 40) as f64;

/// struct sf_spk_model, by value (it is plain data: a clone is a snapshot)
#[derive(Clone)]
struct KModel {
    b: Box<[u64; 8600]>,
}

impl KModel {
    fn new(rate: u32) -> KModel {
        let mut k = KModel { b: Box::new([0; 8600]) };
        assert!(unsafe { sfshim_size() } <= 8600 * 8);
        assert!(unsafe { sf_spk_model_init(k.p(), rate, 0x0f) } == 0);
        k
    }
    fn p(&mut self) -> *mut c_void {
        self.b.as_mut_ptr() as *mut c_void
    }
    fn c(&self) -> *const c_void {
        self.b.as_ptr() as *const c_void
    }
    fn run(&mut self, frames: &[i16]) -> i32 {
        unsafe { sf_spk_model_run(self.p(), frames.as_ptr(), (frames.len() / CH) as u32) }
    }
    fn idle(&mut self, ns: u64) {
        unsafe { sf_spk_model_idle(self.p(), ns) }
    }
    fn set_rate(&mut self, rate: u32) {
        assert!(unsafe { sf_spk_model_set_rate(self.p(), rate) } == 0);
    }
    fn steps(&self) -> u32 {
        unsafe { sf_spk_model_limit_steps(self.c()) }
    }
    fn vol(&self) -> i32 {
        201 - self.steps() as i32
    }
    fn coil(&self, i: usize) -> f64 {
        unsafe { sfshim_coil(self.c(), i as u32) as f64 / Q40 }
    }
    fn magnet(&self, i: usize) -> f64 {
        unsafe { sfshim_magnet(self.c(), i as u32) as f64 / Q40 }
    }
}

const KFAULT: [&str; 4] = ["ok", "coil>140", "magnet>140", "negative power"];

#[derive(Clone, Debug)]
struct Fault {
    kind: String,
    /// number of stream frames consumed when the fault is raised
    frame: u64,
    wall: f64,
    speaker: usize,
}

/// The kernel's side: the model plus the way the kernel calls it.
struct KDriver {
    m: KModel,
    pending: Vec<i16>,
    /// stream frames given to run()
    consumed: u64,
    fault: Option<Fault>,
    /// highest and lowest volume value set since the last period boundary
    vol_hi: i32,
    vol_lo: i32,
}

impl KDriver {
    fn new(rate: u32) -> KDriver {
        KDriver { m: KModel::new(rate), pending: Vec::new(), consumed: 0, fault: None, vol_hi: i32::MIN, vol_lo: i32::MAX }
    }

    fn call(&mut self, n: usize, wall_end: f64, rate: u32) {
        let before = self.m.clone();
        let buf: Vec<i16> = self.pending.drain(..n * CH).collect();
        let ret = self.m.run(&buf);
        if ret != 0 {
            /* the first frame at which the call faults */
            let (mut lo, mut hi) = (1, n);
            while lo < hi {
                let mid = (lo + hi) / 2;
                let mut t = before.clone();
                if t.run(&buf[..mid * CH]) != 0 {
                    hi = mid;
                } else {
                    lo = mid + 1;
                }
            }
            let frame = self.consumed + lo as u64;
            let left = (n - lo) as f64 + (self.pending.len() / CH) as f64;
            self.fault = Some(Fault {
                kind: KFAULT[ret as usize].to_string(),
                frame,
                wall: wall_end - left / rate as f64,
                speaker: unsafe { sfshim_fault_speaker(self.m.c()) } as usize,
            });
        }
        self.consumed += n as u64;
        let v = self.m.vol();
        self.vol_hi = self.vol_hi.max(v);
        self.vol_lo = self.vol_lo.min(v);
    }

    fn feed(&mut self, frames: &[i16], wall_end: f64, rate: u32) {
        self.pending.extend_from_slice(frames);
        while self.fault.is_none() && self.pending.len() >= KCALL * CH {
            self.call(KCALL, wall_end, rate);
        }
        if self.fault.is_some() {
            self.pending.clear();
        }
    }

    fn flush(&mut self, wall: f64, rate: u32) {
        if self.fault.is_none() && !self.pending.is_empty() {
            self.call(self.pending.len() / CH, wall, rate);
        }
    }

    fn pause(&mut self, ns: u64, slice_ns: u64, wall: f64, rate: u32) {
        self.flush(wall, rate);
        let mut left = ns;
        while left > 0 {
            let n = left.min(slice_ns);
            self.m.idle(n);
            left -= n;
        }
    }

    /// the model as it would be with the pending frames run (None: that faults)
    fn at_boundary(&self) -> Option<KModel> {
        let mut m = self.m.clone();
        if !self.pending.is_empty() && m.run(&self.pending) != 0 {
            return None;
        }
        Some(m)
    }
}

// ---------------------------------------------------------------- the reference

struct RDriver {
    ctl: alsa::ctl::Ctl,
    speakers: Vec<types::Speaker>,
    vol_elems: Vec<types::Elem>,
    unlock: types::Elem,
    gain: f32,
    rate: u32,
    buf: Vec<i16>,
    /// pause time since the last period end
    pause_s: f64,
    skips: u64,
    fault: Option<Fault>,
}

impl RDriver {
    /// The daemon's start, cold boot (main.rs).
    fn new(rate: u32) -> RDriver {
        let mut cfg: Ini = Ini::new_cs();
        cfg.load(CONF).expect("Failed to read config file");
        let globals = types::Globals::parse(&cfg);
        assert!(globals.period == PERIOD && globals.channels == CH);
        let ctl: alsa::ctl::Ctl = helpers::open_card("hw:AppleJ293");

        let mut speakers = Vec::new();
        for name in cfg.sections().iter().filter_map(|a| a.strip_prefix("Speaker/")) {
            let s = types::Speaker::new(&globals, name, &cfg, &ctl, true);
            assert!(s.group == 0);
            speakers.push(s);
        }
        assert!(speakers.len() == SPK);

        let mut unlock = types::Elem::new("Speaker Volume Unlock".to_string(), &ctl, alsa::ctl::ElemType::Integer);
        unlock.write_int(&ctl, UNLOCK_MAGIC);
        speakers.iter_mut().for_each(|s| s.update(&ctl, 0.0));

        let vol_elems = alsa::ctl::SPEAKERS
            .iter()
            .map(|s| types::Elem::new(format!("{} Speaker Playback Volume", s), &ctl, alsa::ctl::ElemType::Integer))
            .collect();
        RDriver { ctl, speakers, vol_elems, unlock, gain: 0.0, rate, buf: Vec::new(), pause_s: 0.0, skips: 0, fault: None }
    }

    fn vol(&mut self) -> i32 {
        let v: Vec<i32> = self.vol_elems.iter_mut().map(|e| e.read_int(&self.ctl)).collect();
        assert!(v.iter().all(|x| *x == v[0]));
        v[0]
    }

    /// One iteration of the daemon's loop for the 4096 frames in self.buf.
    /// `end_frame`: stream frames consumed at the end of this period.
    fn period(&mut self, end_frame: u64, wall: f64) {
        let buf = std::mem::take(&mut self.buf);
        let sample_rate = self.rate as i32;

        let pt = PERIOD as f64 / sample_rate as f64;
        let dt = pt + self.pause_s;
        self.pause_s = 0.0;
        /* If we skipped at least 4 periods, run catchup for that minus one */
        if dt > (4f64 * pt) {
            let skip = dt - pt;
            self.speakers.iter_mut().for_each(|s| s.skip_model(skip));
            self.skips += 1;
        }

        let saved: Vec<types::SpeakerState> = self.speakers.iter().map(|s| s.s).collect();
        let speakers = &mut self.speakers;
        let result = catch_unwind(AssertUnwindSafe(|| {
            speakers.iter_mut().map(|s| s.run_model(&buf, sample_rate as f32)).reduce(f32::min).unwrap()
        }));

        match result {
            Ok(gain) => {
                if gain != self.gain {
                    let ctl = &self.ctl;
                    self.speakers.iter_mut().for_each(|s| s.update(ctl, gain));
                    self.gain = gain;
                }
                self.unlock.write_int(&self.ctl, UNLOCK_MAGIC);
                self.ctl.trace.borrow_mut().clear();
            }
            Err(e) => {
                let msg = e.downcast_ref::<String>().cloned().unwrap_or_default();
                let kind = if msg.contains("Coil temperature") {
                    "coil>140"
                } else if msg.contains("Magnet temperature") {
                    "magnet>140"
                } else if msg.contains("Negative power") {
                    "negative power"
                } else {
                    panic!("unexpected panic in the reference: {}", msg)
                };
                /*
                 * The daemon panics once the period is read. For the comparison the
                 * reference is given the best case: a temperature fault counts from the
                 * frame at which the model first exceeds the limit (any speaker).
                 */
                let mut frame = PERIOD;
                let mut speaker = 0;
                if kind != "negative power" {
                    for (i, s) in self.speakers.iter_mut().enumerate() {
                        let mut hot = |n: usize| -> bool {
                            s.s = saved[i];
                            match catch_unwind(AssertUnwindSafe(|| s.run_model(&buf[..n * CH], sample_rate as f32))) {
                                Ok(_) => false,
                                Err(e) => e.downcast_ref::<String>().map(|m| m.contains("temperature")).unwrap_or(false),
                            }
                        };
                        if !hot(PERIOD) {
                            continue;
                        }
                        let (mut lo, mut hi) = (1, PERIOD);
                        while lo < hi {
                            let mid = (lo + hi) / 2;
                            if hot(mid) {
                                hi = mid;
                            } else {
                                lo = mid + 1;
                            }
                        }
                        if lo < frame {
                            frame = lo;
                            speaker = i;
                        }
                    }
                } else {
                    speaker = NAMES.iter().position(|n| msg.starts_with(match *n {
                        "LF" => "Left Front",
                        "RF" => "Right Front",
                        "LR" => "Left Rear",
                        _ => "Right Rear",
                    })).unwrap_or(0);
                }
                let frame_abs = end_frame - PERIOD as u64 + frame as u64;
                self.fault = Some(Fault {
                    kind: kind.to_string(),
                    frame: frame_abs,
                    wall: wall - (PERIOD - frame) as f64 / self.rate as f64,
                    speaker,
                });
            }
        }
    }
}

// ---------------------------------------------------------------- inputs

struct Rng(u64);

impl Rng {
    fn next(&mut self) -> u64 {
        self.0 = self.0.wrapping_add(0x9e3779b97f4a7c15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xbf58476d1ce4e5b9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94d049bb133111eb);
        z ^ (z >> 31)
    }
    fn f(&mut self) -> f64 {
        (self.next() >> 11) as f64 / (1u64 << 53) as f64
    }
    fn range(&mut self, lo: f64, hi: f64) -> f64 {
        lo + (hi - lo) * self.f()
    }
    fn log(&mut self, lo: f64, hi: f64) -> f64 {
        (lo.ln() + (hi.ln() - lo.ln()) * self.f()).exp()
    }
    fn int(&mut self, lo: i64, hi: i64) -> i64 {
        lo + (self.next() % (hi - lo + 1) as u64) as i64
    }
    fn chance(&mut self, p: f64) -> bool {
        self.f() < p
    }
}

#[derive(Clone, Debug)]
enum Sig {
    /// both samples constant
    Const { i: i16, v: i16 },
    /// uniform noise of +/- ia counts on the current and +/- va on the voltage
    Noise { ia: i64, va: i64 },
    /// v = vp sin(wt), i = ip sin(wt - phi): volts, amperes, radians
    Sine { vp: f64, ip: f64, phi: f64 },
    /// resistive sine whose mean power swings between two levels
    Hover { p_lo: f64, p_hi: f64, period_s: f64 },
    /// +32767 / -32768 alternating frame by frame on both channels
    Alt,
    /// one channel stuck, the other a sine of the given peak in counts
    IStuck { i: i16, v_counts: f64 },
    VStuck { v: i16, i_counts: f64 },
    /// random walks; resistive: the current follows the voltage (i = v / 9.7 ohm)
    Walk { step: i64, resistive: bool },
}

fn zero() -> Sig {
    Sig::Const { i: 0, v: 0 }
}

/// resistive sine of mean power `p` watts into 9.7 ohm
fn res(p: f64) -> Sig {
    let vp = (2.0 * p * Z_NOMINAL).sqrt();
    Sig::Sine { vp, ip: vp / Z_NOMINAL, phi: 0.0 }
}

/// sine of apparent power `s` VA with the current `deg` degrees behind the voltage
fn reactive(s: f64, deg: f64) -> Sig {
    let vp = (2.0 * s * Z_NOMINAL).sqrt();
    Sig::Sine { vp, ip: vp / Z_NOMINAL, phi: deg.to_radians() }
}

fn idle_noise() -> Sig {
    Sig::Noise { ia: 60, va: 35 }
}

#[derive(Clone, Debug)]
enum Seg {
    /// frames, the four speakers' signals, an on/off gate in frames (off: zero samples)
    Play(u64, [Sig; SPK], Option<(u64, u64)>),
    /// the stream stops for this many nanoseconds
    Pause(u64),
    /// the next stream has this sample rate
    Rate(u32),
}

fn all(s: Sig) -> [Sig; SPK] {
    [s.clone(), s.clone(), s.clone(), s]
}

#[derive(Clone)]
struct Scenario {
    name: String,
    class: &'static str,
    rate: u32,
    seed: u64,
    tone_hz: f64,
    idle_slice_ns: u64,
    segs: Vec<Seg>,
}

fn counts(x: f64, scale: f64) -> i16 {
    (x / scale * 32768.0).round().clamp(-32768.0, 32767.0) as i16
}

struct Gen {
    rng: Rng,
    walk: [(i64, i64); SPK],
    /// stream frame number, for the phase of the tones
    n: u64,
}

impl Gen {
    fn frame(&mut self, sigs: &[Sig; SPK], tone_hz: f64, rate: u32, out: &mut Vec<i16>) {
        let t = self.n as f64 / rate as f64;
        let (s0, c0) = (2.0 * std::f64::consts::PI * tone_hz * t).sin_cos();
        for (k, sig) in sigs.iter().enumerate() {
            let (i, v) = match sig {
                Sig::Const { i, v } => (*i, *v),
                Sig::Noise { ia, va } => (self.rng.int(-ia, *ia) as i16, self.rng.int(-va, *va) as i16),
                Sig::Sine { vp, ip, phi } => {
                    let si = s0 * phi.cos() - c0 * phi.sin();
                    (counts(ip * si, IS_SCALE), counts(vp * s0, VS_SCALE))
                }
                Sig::Hover { p_lo, p_hi, period_s } => {
                    let p = p_lo + (p_hi - p_lo) * (0.5 + 0.5 * (2.0 * std::f64::consts::PI * t / period_s).sin());
                    let vp = (2.0 * p * Z_NOMINAL).sqrt();
                    (counts(vp / Z_NOMINAL * s0, IS_SCALE), counts(vp * s0, VS_SCALE))
                }
                Sig::Alt => {
                    let x = if self.n % 2 == 0 { 32767 } else { -32768 };
                    (x, x)
                }
                Sig::IStuck { i, v_counts } => (*i, (v_counts * s0).round().clamp(-32768.0, 32767.0) as i16),
                Sig::VStuck { v, i_counts } => ((i_counts * s0).round().clamp(-32768.0, 32767.0) as i16, *v),
                Sig::Walk { step, resistive } => {
                    let w = &mut self.walk[k];
                    w.1 = (w.1 + self.rng.int(-step, *step)).clamp(-32768, 32767);
                    if *resistive {
                        let volts = w.1 as f64 / 32768.0 * VS_SCALE;
                        (counts(volts / Z_NOMINAL, IS_SCALE), w.1 as i16)
                    } else {
                        w.0 = (w.0 + self.rng.int(-step, *step)).clamp(-32768, 32767);
                        (w.0 as i16, w.1 as i16)
                    }
                }
            };
            out.push(i);
            out.push(v);
        }
        self.n += 1;
    }
}

// ---------------------------------------------------------------- one scenario

#[derive(Clone, Default)]
struct Res {
    name: String,
    class: &'static str,
    #[allow(dead_code)]
    rate: u32,
    wall_s: f64,
    stream_s: f64,
    periods: u64,
    skips: u64,
    dcoil: (f64, f64),
    dmag: (f64, f64),
    /// where the minimum of kernel - reference was: (wall, speaker, kernel, reference)
    dcoil_min_at: (f64, usize, f64, f64),
    dmag_min_at: (f64, usize, f64, f64),
    /// kernel volume - reference volume at period ends: (most conservative, least)
    dvol: (i32, i32),
    dvol_max_at: (f64, i32, i32),
    /// period ends at which the reference limits and the kernel reports no limit
    unlimited: u64,
    /// highest kernel volume inside a period minus the reference volume in force during it
    inside: i32,
    /// kernel volume minus the value the reference's temperatures alone ask for (its
    /// gain formula and rounding, without its hysteresis), largest
    dvol_nohyst: i32,
    ref_limited_periods: u64,
    ref_min_vol: i32,
    k_min_vol: i32,
    max_coil: (f64, f64),
    rfault: Option<Fault>,
    kfault: Option<Fault>,
    viol: Vec<String>,
}

fn run_scenario(sc: &Scenario) -> Res {
    let mut rate = sc.rate;
    let mut k = KDriver::new(rate);
    let mut r = RDriver::new(rate);
    let mut gen = Gen { rng: Rng(sc.seed), walk: [(0, 0); SPK], n: 0 };
    let mut res = Res {
        name: sc.name.clone(),
        class: sc.class,
        rate,
        dcoil: (f64::INFINITY, f64::NEG_INFINITY),
        dmag: (f64::INFINITY, f64::NEG_INFINITY),
        dvol: (i32::MAX, i32::MIN),
        inside: i32::MIN,
        dvol_nohyst: i32::MIN,
        ref_min_vol: 201,
        k_min_vol: 201,
        ..Default::default()
    };
    let mut crit = [false; 4];
    let mut frame: u64 = 0;
    let mut wall = 0f64;
    let mut ref_in_force = 201;
    let mut chunk: Vec<i16> = Vec::with_capacity(PERIOD * CH);

    for seg in &sc.segs {
        match seg {
            Seg::Rate(new) => {
                k.flush(wall, rate);
                if k.fault.is_none() {
                    k.m.set_rate(*new);
                }
                rate = *new;
                r.rate = rate;
            }
            Seg::Pause(ns) => {
                if k.fault.is_none() {
                    k.pause(*ns, sc.idle_slice_ns, wall, rate);
                }
                r.pause_s += *ns as f64 / 1e9;
                wall += *ns as f64 / 1e9;
            }
            Seg::Play(frames, sigs, gate) => {
                let silent = all(zero());
                let mut done = 0u64;
                while done < *frames {
                    if r.fault.is_some() && k.fault.is_some() {
                        break;
                    }
                    let room = if r.fault.is_none() { PERIOD - r.buf.len() / CH } else { PERIOD };
                    let n = (*frames - done).min(room as u64);
                    chunk.clear();
                    for j in 0..n {
                        let on = match gate {
                            Some((on, off)) => (done + j) % (on + off) < *on,
                            None => true,
                        };
                        gen.frame(if on { sigs } else { &silent }, sc.tone_hz, rate, &mut chunk);
                    }
                    done += n;
                    frame += n;
                    wall += n as f64 / rate as f64;
                    res.stream_s += n as f64 / rate as f64;

                    if k.fault.is_none() {
                        k.feed(&chunk, wall, rate);
                    }
                    if r.fault.is_some() {
                        continue;
                    }
                    r.buf.extend_from_slice(&chunk);
                    if r.buf.len() < PERIOD * CH {
                        continue;
                    }

                    /* a period of the reference is complete */
                    r.period(frame, wall);
                    if r.fault.is_some() {
                        continue;
                    }
                    let rvol = r.vol();
                    if rvol < 201 {
                        res.ref_limited_periods += 1;
                    }
                    res.ref_min_vol = res.ref_min_vol.min(rvol);
                    if k.fault.is_some() {
                        continue;
                    }
                    let km = match k.at_boundary() {
                        Some(km) => km,
                        None => continue, /* the kernel faults in the frames it has not run yet */
                    };
                    res.periods += 1;

                    /* (1) temperatures */
                    for i in 0..SPK {
                        let (kc, rc) = (km.coil(i), r.speakers[i].s.t_coil);
                        let (kmg, rm) = (km.magnet(i), r.speakers[i].s.t_magnet);
                        res.max_coil = (res.max_coil.0.max(kc), res.max_coil.1.max(rc));
                        if kc - rc < res.dcoil.0 {
                            res.dcoil.0 = kc - rc;
                            res.dcoil_min_at = (wall, i, kc, rc);
                        }
                        res.dcoil.1 = res.dcoil.1.max(kc - rc);
                        if kmg - rm < res.dmag.0 {
                            res.dmag.0 = kmg - rm;
                            res.dmag_min_at = (wall, i, kmg, rm);
                        }
                        res.dmag.1 = res.dmag.1.max(kmg - rm);
                        if !crit[0] && (kc < rc || kmg < rm) {
                            crit[0] = true;
                            res.viol.push(format!(
                                "(1) first at t={:.3} s (frame {}), {}: coil kernel {:.4} reference {:.4}, magnet kernel {:.4} reference {:.4}",
                                wall, frame, NAMES[i], kc, rc, kmg, rm
                            ));
                        }
                    }

                    /* (2) volume control values */
                    let kvol = km.vol();
                    res.k_min_vol = res.k_min_vol.min(kvol);
                    res.dvol.0 = res.dvol.0.min(kvol - rvol);
                    if kvol - rvol > res.dvol.1 {
                        res.dvol.1 = kvol - rvol;
                        res.dvol_max_at = (wall, kvol, rvol);
                    }
                    if rvol < 201 && kvol >= 201 {
                        res.unlimited += 1;
                    }
                    /* (2n) the reference's gain formula on its temperatures without its hysteresis */
                    {
                        let temp = r.speakers.iter().map(|s| (s.s.t_coil as f32).max(s.s.t_magnet as f32)).fold(f32::MIN, f32::max);
                        let reduction = (temp - (130.0f32 - 20.0f32)) / 20.0f32;
                        let mut gain = r.speakers[0].s.min_gain * reduction.max(0.);
                        if gain > -0.01 {
                            gain = 0.;
                        }
                        let mb = (gain * 100.0) as i64;
                        let v = if mb <= -10050 { 0 } else if mb >= 0 { 201 } else { ((mb + 10050) * 201 + 5025) / 10050 } as i32;
                        res.dvol_nohyst = res.dvol_nohyst.max(kvol - v);
                        if !crit[3] && kvol > v {
                            crit[3] = true;
                            res.viol.push(format!(
                                "(2n) first at t={:.3} s (frame {}): kernel volume {} > {} that the reference's temperatures ask for without hysteresis (hottest {:.4})",
                                wall, frame, kvol, v, temp
                            ));
                        }
                    }
                    if !crit[1] && kvol > rvol {
                        crit[1] = true;
                        let i = (0..SPK).max_by(|a, b| r.speakers[*a].s.t_coil.total_cmp(&r.speakers[*b].s.t_coil)).unwrap();
                        res.viol.push(format!(
                            "(2) first at t={:.3} s (frame {}): kernel volume {} > reference {} (hottest {}: coil kernel {:.3} reference {:.3}, reference hysteresis {:.3}/{:.3})",
                            wall, frame, kvol, rvol, NAMES[i], km.coil(i), r.speakers[i].s.t_coil,
                            r.speakers[i].s.t_coil_hyst, r.speakers[i].s.t_magnet_hyst
                        ));
                    }
                    /* the kernel's values inside this period against what the reference had set for it */
                    if k.vol_hi != i32::MIN {
                        res.inside = res.inside.max(k.vol_hi - ref_in_force);
                    }
                    k.vol_hi = i32::MIN;
                    k.vol_lo = i32::MAX;
                    ref_in_force = rvol;
                }
            }
        }
    }
    k.flush(wall, rate);

    /* (3) faults */
    if let Some(rf) = &r.fault {
        match &k.fault {
            Some(kf) if kf.frame <= rf.frame => {}
            Some(kf) => res.viol.push(format!(
                "(3) reference: {} ({}) at frame {} (t={:.3} s); kernel only at frame {} (t={:.3} s): {} ({})",
                rf.kind, NAMES[rf.speaker], rf.frame, rf.wall, kf.frame, kf.wall, kf.kind, NAMES[kf.speaker]
            )),
            None => res.viol.push(format!(
                "(3) reference: {} ({}) at frame {} (t={:.3} s); the kernel raises no fault (to the end, t={:.3} s)",
                rf.kind, NAMES[rf.speaker], rf.frame, rf.wall, wall
            )),
        }
    }
    res.wall_s = wall;
    res.skips = r.skips;
    res.rfault = r.fault.clone();
    res.kfault = k.fault.clone();
    res
}

// ---------------------------------------------------------------- the scenarios

fn fr(secs: f64, rate: u32) -> u64 {
    ((secs * rate as f64 / PERIOD as f64).round() as u64).max(1) * PERIOD as u64
}

fn ns(secs: f64) -> u64 {
    (secs * 1e9).round() as u64
}

fn designed(rate: u32) -> Vec<Scenario> {
    let mut v: Vec<Scenario> = Vec::new();
    let mut seed = 1000u64;
    let mut add = |class: &'static str, name: &str, segs: Vec<Seg>| {
        seed += 1;
        v.push(Scenario {
            name: format!("{}@{}", name, rate),
            class,
            rate,
            seed,
            tone_hz: 997.0,
            idle_slice_ns: 100_000_000,
            segs,
        });
    };
    let play = |secs: f64, s: Sig| Seg::Play(fr(secs, rate), all(s), None);
    let gated = |secs: f64, s: Sig, on: f64, off: f64| {
        Seg::Play(fr(secs, rate), all(s), Some(((on * rate as f64) as u64, (off * rate as f64) as u64)))
    };

    /* ---- the list asked for ---- */
    add("asked", "silence 5min", vec![play(300.0, zero())]);
    add("asked", "idle noise 5min", vec![play(300.0, idle_noise())]);
    add("asked", "idle noise 30min", vec![play(1800.0, idle_noise())]);
    for (label, p) in [
        ("tiny (1 count)", 0.5 * (14.0f64 / 32768.0).powi(2) / Z_NOMINAL),
        ("0.001W", 0.001), ("0.01W", 0.01), ("0.1W", 0.1), ("0.5W", 0.5),
        ("0.68W", 0.68), ("0.85W", 0.85), ("0.90W", 0.90), ("0.915W", 0.915), ("0.93W", 0.93),
        ("1.0W", 1.0), ("1.03W", 1.03), ("1.1W", 1.1), ("1.5W", 1.5), ("3W", 3.0), ("6W", 6.0),
    ] {
        add("asked", &format!("sine {} 5min", label), vec![play(300.0, res(p))]);
    }
    add("asked", "sine 0.915W 30min", vec![play(1800.0, res(0.915))]);
    add("asked", "sine 1.0W 30min", vec![play(1800.0, res(1.0))]);
    add("asked", "sine full-scale V (10.1W) 1min", vec![play(60.0, Sig::Sine { vp: 14.0, ip: 14.0 / Z_NOMINAL, phi: 0.0 })]);
    add("asked", "sine full-scale V and I (26W) 1min", vec![play(60.0, Sig::Sine { vp: 14.0, ip: 3.75, phi: 0.0 })]);
    add("asked", "sine clipped x4 (square) 1min", vec![play(60.0, Sig::Sine { vp: 56.0, ip: 15.0, phi: 0.0 })]);

    add("asked", "burst 1s on/1s off 1.8W 10min", vec![gated(600.0, res(1.8), 1.0, 1.0)]);
    add("asked", "burst 1s on/1s off 4W 10min", vec![gated(600.0, res(4.0), 1.0, 1.0)]);
    add("asked", "burst 10s on/60s off 5W 14min", vec![gated(840.0, res(5.0), 10.0, 60.0)]);
    add("asked", "burst 10s on/60s off 2W 14min", vec![gated(840.0, res(2.0), 10.0, 60.0)]);
    add("asked", "burst 100ms on/900ms off 9W 10min", vec![gated(600.0, res(9.0), 0.1, 0.9)]);
    add("asked", "burst 100ms on/900ms off 10.1W 10min", vec![gated(600.0, Sig::Sine { vp: 14.0, ip: 14.0 / Z_NOMINAL, phi: 0.0 }, 0.1, 0.9)]);

    add("asked", "hover at onset 0.66-0.71W 20min", vec![play(1200.0, Sig::Hover { p_lo: 0.66, p_hi: 0.71, period_s: 20.0 })]);
    add("asked", "hover at onset 0.60-0.80W 20min", vec![play(1200.0, Sig::Hover { p_lo: 0.60, p_hi: 0.80, period_s: 7.0 })]);
    add("asked", "hover 0.6865W steady 20min", vec![play(1200.0, res(0.6865))]);

    for pause in [0.5, 5.0, 60.0, 600.0] {
        add(
            "asked",
            &format!("heat 1.0W 60s, pause {}s, heat 60s", pause),
            vec![play(60.0, res(1.0)), Seg::Pause(ns(pause)), play(60.0, res(1.0))],
        );
        add(
            "asked",
            &format!("heat 2.5W 20s, pause {}s, x4", pause),
            (0..4).flat_map(|_| vec![play(20.0, res(2.5)), Seg::Pause(ns(pause))]).chain([play(20.0, res(0.3))]).collect(),
        );
    }

    add("asked", "reactive PF 0.5, 2 VA 5min", vec![play(300.0, reactive(2.0, 60.0))]);
    add("asked", "reactive PF 0.5, 6 VA 5min", vec![play(300.0, reactive(6.0, 60.0))]);
    add("asked", "reactive PF 0, 5 VA 5min", vec![play(300.0, reactive(5.0, 90.0))]);
    add("asked", "reactive PF -0.5, 1 VA 1min", vec![play(60.0, reactive(1.0, 120.0))]);
    add("asked", "reactive PF -1, 1 VA 1min", vec![play(60.0, reactive(1.0, 180.0))]);
    add("asked", "reactive PF -1, 0.012 W 1min", vec![play(60.0, reactive(0.012, 180.0))]);
    add("asked", "reactive PF -1, 0.008 W 5min", vec![play(300.0, reactive(0.008, 180.0))]);
    add("asked", "heat 1W 60s then PF -1 1 VA", vec![play(60.0, res(1.0)), play(10.0, reactive(1.0, 180.0))]);

    add("asked", "stuck all +32767", vec![play(10.0, Sig::Const { i: 32767, v: 32767 })]);
    add("asked", "stuck all -32768", vec![play(10.0, Sig::Const { i: -32768, v: -32768 })]);
    add("asked", "stuck I +32767, V -32768", vec![play(10.0, Sig::Const { i: 32767, v: -32768 })]);
    add("asked", "stuck I +32767, V sine", vec![play(120.0, Sig::IStuck { i: 32767, v_counts: 20000.0 })]);
    add("asked", "stuck I -32768, V sine", vec![play(120.0, Sig::IStuck { i: -32768, v_counts: 20000.0 })]);
    add("asked", "stuck I 0, V sine", vec![play(120.0, Sig::IStuck { i: 0, v_counts: 20000.0 })]);
    add("asked", "stuck V +32767, I sine", vec![play(120.0, Sig::VStuck { v: 32767, i_counts: 20000.0 })]);
    add("asked", "stuck V 0, I sine", vec![play(120.0, Sig::VStuck { v: 0, i_counts: 20000.0 })]);
    add("asked", "stuck small DC (I 300, V 3000)", vec![play(300.0, Sig::Const { i: 3000, v: 6000 })]);
    add("asked", "alternating +32767/-32768", vec![play(10.0, Sig::Alt)]);

    add("asked", "speakers differ: 1.2W / 0.5W / silence / noise", vec![Seg::Play(fr(600.0, rate), [res(1.2), res(0.5), zero(), idle_noise()], None)]);
    add("asked", "speakers differ: only RR hot 1.0W", vec![Seg::Play(fr(600.0, rate), [zero(), idle_noise(), res(0.2), res(1.0)], None)]);
    add("asked", "speakers differ: hot one moves", vec![
        Seg::Play(fr(90.0, rate), [res(1.4), zero(), zero(), zero()], None),
        Seg::Play(fr(90.0, rate), [zero(), res(1.4), zero(), zero()], None),
        Seg::Play(fr(90.0, rate), [zero(), zero(), res(1.4), zero()], None),
        Seg::Play(fr(90.0, rate), [zero(), zero(), zero(), res(1.4)], None),
        Seg::Play(fr(90.0, rate), all(res(0.2)), None),
    ]);
    add("asked", "speakers differ: one faults (LR 8W)", vec![Seg::Play(fr(60.0, rate), [res(0.5), res(0.5), res(8.0), res(0.5)], None)]);

    for (i, step) in [40, 400, 3000].iter().enumerate() {
        add("asked", &format!("random walk resistive #{}", i), vec![play(600.0, Sig::Walk { step: *step, resistive: true })]);
        add("asked", &format!("random walk I,V independent #{}", i), vec![play(600.0, Sig::Walk { step: *step, resistive: false })]);
    }

    /* ---- probes from reading sf_speaker_model.c against types.rs ---- */
    /* pauses the daemon does not see (it only catches up when dt > 4 periods) */
    add("probe", "pause 0.25s every 1s at 1.0W, 5min", (0..300).flat_map(|_| vec![play(1.0, res(1.0)), Seg::Pause(ns(0.25))]).collect());
    add("probe", "heat 1.0W 60s, pause 0.2s, heat 5s", vec![play(60.0, res(1.0)), Seg::Pause(ns(0.2)), play(5.0, res(1.0))]);
    /* a pause that is not on a 4096-frame boundary: the daemon applies it before the whole period */
    add("probe", "heat 0.9W 40s, 3072 frames 9W, pause 60s inside a period", vec![
        play(40.0, res(0.9)),
        Seg::Play(3072, all(res(9.0)), None),
        Seg::Pause(ns(60.0)),
        Seg::Play(1024, all(zero()), None),
        play(5.0, zero()),
    ]);
    /* ripple inside a period while the envelope falls: the kernel's hysteresis follows the dips */
    add("probe", "heat 1.0W 60s, then 1024 on/3072 off 3.6W", vec![play(60.0, res(1.0)), gated(120.0, res(3.6), 1024.0 / rate as f64, 3072.0 / rate as f64)]);
    add("probe", "heat 1.0W 60s, then 2048 on/2048 off 1.8W", vec![play(60.0, res(1.0)), gated(120.0, res(1.8), 2048.0 / rate as f64, 2048.0 / rate as f64)]);
    add("probe", "heat 1.0W 60s, then 3072 off/1024 on 3.0W (period ends on the peak)", vec![
        play(60.0, res(1.0)),
        Seg::Play(3072, all(zero()), None),
        gated(120.0, res(3.0), 1024.0 / rate as f64, 3072.0 / rate as f64),
    ]);
    add("probe", "heat 1.0W 60s, pause 2s, 4096 frames 6W, then 0.5W (hysteresis across a pause)", vec![
        play(60.0, res(1.0)),
        Seg::Pause(ns(2.0)),
        Seg::Play(4096, all(res(6.0)), None),
        play(20.0, res(0.5)),
    ]);
    /* the power window restarts at every pause */
    add("probe", "negative power in 3072-frame streams, 150ms pauses", (0..200).flat_map(|_| vec![Seg::Play(3072, all(reactive(1.0, 180.0)), None), Seg::Pause(ns(0.15))]).collect());
    add("probe", "negative power in 2048-frame streams, 1s pauses", (0..60).flat_map(|_| vec![Seg::Play(2048, all(reactive(1.0, 180.0)), None), Seg::Pause(ns(1.0))]).collect());
    add("probe", "negative power -0.05W in 4000-frame streams, 0.5s pauses, 10min", (0..1200).flat_map(|_| vec![Seg::Play(4000, all(reactive(0.05, 180.0)), None), Seg::Pause(ns(0.5))]).collect());
    /* the windows of the two are not aligned after a stream that is not a multiple of 4096 frames */
    add("probe", "negative power -1W for 4096 frames across two kernel windows", vec![
        Seg::Play(2048, all(res(1.0)), None),
        Seg::Pause(ns(1.0)),
        Seg::Play(2048, all(res(1.0)), None),
        Seg::Play(4096, all(reactive(1.0, 180.0)), None),
        Seg::Play(2048, all(res(1.0)), None),
        play(5.0, res(0.1)),
    ]);
    /* idle(): a coil below the magnet (negative products) is not warmed by the magnet */
    add("probe", "full scale +26W/-26VA in one period, then pause 30s", vec![
        Seg::Play(2048, all(Sig::Sine { vp: 14.0, ip: 3.75, phi: 0.0 }), None),
        Seg::Play(2048, all(Sig::Sine { vp: 14.0, ip: 3.75, phi: std::f64::consts::PI }), None),
        Seg::Pause(ns(30.0)),
        play(5.0, zero()),
    ]);
    v
}

#[derive(Clone, Copy, PartialEq)]
enum Kind {
    /// what the daemon is built for: every stream a whole number of 4096-frame periods,
    /// pauses long enough for it to notice (more than 4 periods), power factor >= 0
    Clean,
    /// the same streams and pauses, any samples (negative products, stuck, walks)
    Aligned,
    /// any stream length, any pause, any samples
    Any,
}

fn random_sig(g: &mut Rng, clean: bool) -> Sig {
    let x = g.f();
    let power = |g: &mut Rng| {
        if g.chance(0.6) {
            g.log(0.4, 3.0)
        } else {
            g.log(1e-3, 12.0)
        }
    };
    if x < 0.10 {
        zero()
    } else if x < 0.20 {
        idle_noise()
    } else if x < 0.70 {
        res(power(g))
    } else if x < 0.85 || clean {
        reactive(power(g), g.range(0.0, 89.0))
    } else if x < 0.90 {
        reactive(power(g), g.range(89.0, 180.0))
    } else if x < 0.95 {
        Sig::Walk { step: g.int(5, 4000), resistive: g.chance(0.5) }
    } else if x < 0.97 {
        Sig::Const { i: g.int(-32768, 32767) as i16, v: g.int(-32768, 32767) as i16 }
    } else if x < 0.99 {
        Sig::IStuck { i: g.int(-32768, 32767) as i16, v_counts: g.log(10.0, 32767.0) }
    } else {
        Sig::Alt
    }
}

fn random(seed: u64, kind: Kind) -> Scenario {
    let clean = kind == Kind::Clean;
    let aligned = kind != Kind::Any;
    let mut g = Rng(seed.wrapping_mul(0x2545f4914f6cdd1d) ^ 0x5eed);
    let mut rate = if g.chance(0.5) { 48000 } else { 44100 };
    let first_rate = rate;
    let mut segs = Vec::new();
    let mut left = g.range(40.0, 200.0);
    let mut last_pause = true;
    while left > 0.0 {
        if !last_pause && g.chance(0.3) {
            let p = if aligned { g.log(0.4, 120.0) } else { g.log(0.005, 120.0) };
            segs.push(Seg::Pause(ns(p)));
            if g.chance(0.1) {
                rate = if rate == 48000 { 44100 } else { 48000 };
                segs.push(Seg::Rate(rate));
            }
            last_pause = true;
            continue;
        }
        let secs = g.log(0.05, 40.0);
        let frames = if aligned { fr(secs, rate) } else { ((secs * rate as f64) as u64).max(1) };
        let sigs = if g.chance(0.6) {
            all(random_sig(&mut g, clean))
        } else {
            [random_sig(&mut g, clean), random_sig(&mut g, clean), random_sig(&mut g, clean), random_sig(&mut g, clean)]
        };
        let gate = if g.chance(0.3) {
            Some((g.log(256.0, 96000.0) as u64, g.log(256.0, 96000.0) as u64))
        } else {
            None
        };
        segs.push(Seg::Play(frames, sigs, gate));
        left -= secs;
        last_pause = false;
    }
    Scenario {
        name: format!("random-{}-{}", match kind { Kind::Clean => "clean", Kind::Aligned => "aligned", Kind::Any => "any" }, seed),
        class: match kind { Kind::Clean => "random-clean", Kind::Aligned => "random-aligned", Kind::Any => "random-any" },
        rate: first_rate,
        seed,
        tone_hz: 997.0,
        idle_slice_ns: if aligned { 100_000_000 } else { g.int(80_000_000, 120_000_000) as u64 },
        segs,
    }
}

// ---------------------------------------------------------------- report

fn fault_text(f: &Option<Fault>) -> String {
    match f {
        Some(f) => format!("{} {} @{:.3}s", f.kind, NAMES[f.speaker], f.wall),
        None => "-".to_string(),
    }
}

fn fmt_d(d: (f64, f64)) -> String {
    if d.0.is_finite() {
        format!("{:+9.4} {:+9.4}", d.0, d.1)
    } else {
        format!("{:>9} {:>9}", "-", "-")
    }
}

fn row(r: &Res) -> String {
    let (dv0, dv1, inside) = if r.periods > 0 {
        (format!("{:+}", r.dvol.0), format!("{:+}", r.dvol.1), format!("{:+}", r.inside))
    } else {
        ("-".to_string(), "-".to_string(), "-".to_string())
    };
    format!(
        "{:<4} {:<52} {:>7.0} {:>6} {} {} {:>4} {:>4} {:>5} {:>6} {:>4} {:>4}  {:<26} {:<26}",
        if r.viol.is_empty() { "ok" } else { "FAIL" },
        r.name,
        r.wall_s,
        r.periods,
        fmt_d(r.dcoil),
        fmt_d(r.dmag),
        dv0,
        dv1,
        r.unlimited,
        inside,
        r.ref_min_vol,
        r.k_min_vol,
        fault_text(&r.rfault),
        fault_text(&r.kfault),
    )
}

const HEADER: &str = "     scenario                                              wall s  cmp'd  dcoil min       max  dmag min       max dvol  max  nolim inside rmin kmin  reference fault            kernel fault";

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let arg = |name: &str, default: &str| -> String {
        args.iter().position(|a| a == name).and_then(|i| args.get(i + 1).cloned()).unwrap_or(default.to_string())
    };
    let n_random: u64 = arg("--random", "4500").parse().unwrap();
    let threads: usize = arg("--threads", "16").parse().unwrap();
    let only = arg("--only", "");
    let out = arg("--out", "");
    let label = arg("--label", "");
    let no_designed = args.iter().any(|a| a == "--no-designed");

    /* the reference panics on purpose; keep its messages out of the output */
    std::panic::set_hook(Box::new(|info| {
        let msg = info.payload().downcast_ref::<String>().cloned().unwrap_or_default();
        if !(msg.contains("temperature limit exceeded") || msg.contains("Negative power")) {
            eprintln!("{}", info);
        }
    }));

    let mut scenarios: Vec<Scenario> = Vec::new();
    if !no_designed {
        scenarios.extend(designed(48000));
        scenarios.extend(designed(44100));
    }
    for seed in 0..n_random {
        scenarios.push(random(seed, [Kind::Clean, Kind::Aligned, Kind::Any][(seed % 3) as usize]));
    }
    if !only.is_empty() {
        scenarios.retain(|s| s.name.contains(&only));
    }
    if args.iter().any(|a| a == "--dump") {
        /* the segments of the chosen scenarios, to read a counterexample by hand */
        for sc in &scenarios {
            println!("{} rate {} idle slice {} ns", sc.name, sc.rate, sc.idle_slice_ns);
            for seg in &sc.segs {
                match seg {
                    Seg::Play(n, ..) => println!("  play {} frames", n),
                    Seg::Pause(ns) => println!("  pause {:.6} s", *ns as f64 / 1e9),
                    Seg::Rate(r) => println!("  rate {}", r),
                }
            }
        }
    }

    let next = AtomicUsize::new(0);
    let results: Mutex<BTreeMap<usize, Res>> = Mutex::new(BTreeMap::new());
    std::thread::scope(|scope| {
        for _ in 0..threads {
            scope.spawn(|| loop {
                let i = next.fetch_add(1, Ordering::Relaxed);
                if i >= scenarios.len() {
                    break;
                }
                let r = run_scenario(&scenarios[i]);
                results.lock().unwrap().insert(i, r);
            });
        }
    });
    let results: Vec<Res> = results.into_inner().unwrap().into_values().collect();

    let mut text = String::new();
    let k48 = KModel::new(48000);
    let k44 = KModel::new(44100);
    writeln!(text, "kernel speaker model against the reference (upstream speakersafetyd model code){}", if label.is_empty() { String::new() } else { format!(" - {}", label) }).unwrap();
    writeln!(text, "per-sample filter coefficients, kernel (Q0.40) / reference (f32 step, as types.rs computes them):").unwrap();
    for (rate, k) in [(48000u32, &k48), (44100u32, &k44)] {
        let step = 1f32 / rate as f32;
        let (rc, rm) = ((step / (2.80f32 + step)) as f64, (step / (79.70f32 + step)) as f64);
        let (kc, km) = unsafe { (sfshim_alpha_coil(k.c()) as f64 / Q40, sfshim_alpha_magnet(k.c()) as f64 / Q40) };
        writeln!(text, "  {} Hz: coil {:.12e} / {:.12e} (kernel/reference - 1 = {:+.2e}), magnet {:.12e} / {:.12e} ({:+.2e})",
            rate, kc, rc, kc / rc - 1.0, km, rm, km / rm - 1.0).unwrap();
    }
    writeln!(text).unwrap();
    writeln!(text, "columns: wall s = scenario length; cmp'd = period ends compared (both alive); dcoil/dmag = kernel - reference in C").unwrap();
    writeln!(text, "  over all speakers and period ends (min < 0 violates (1)); dvol = kernel - reference volume value at period ends").unwrap();
    writeln!(text, "  (max > 0 violates (2)); nolim = period ends where the reference limits and the kernel does not; inside = highest").unwrap();
    writeln!(text, "  kernel value inside a period minus the reference value in force during it (informative); rmin/kmin = lowest").unwrap();
    writeln!(text, "  volume value each set; faults with speaker and wall time ((3): the kernel's must not be later).").unwrap();
    writeln!(text).unwrap();

    let mut failed = 0;
    for class in ["asked", "probe"] {
        let rows: Vec<&Res> = results.iter().filter(|r| r.class == class).collect();
        if rows.is_empty() {
            continue;
        }
        writeln!(text, "== {} ==", if class == "asked" { "scenarios asked for" } else { "probes written after reading the kernel model" }).unwrap();
        writeln!(text, "{}", HEADER).unwrap();
        for r in rows {
            writeln!(text, "{}", row(r)).unwrap();
            for v in &r.viol {
                writeln!(text, "       {}", v).unwrap();
            }
            if !r.viol.is_empty() {
                failed += 1;
            }
        }
        writeln!(text).unwrap();
    }

    for class in ["random-clean", "random-aligned", "random-any"] {
        let rows: Vec<&Res> = results.iter().filter(|r| r.class == class).collect();
        if rows.is_empty() {
            continue;
        }
        let bad: Vec<&&Res> = rows.iter().filter(|r| !r.viol.is_empty()).collect();
        let count = |c: &str| bad.iter().filter(|r| r.viol.iter().any(|v| v.starts_with(c))).count();
        writeln!(text, "== {}: {} scenarios, {} with violations ((1) {}, (2) {}, (2n) {}, (3) {}) ==", class, rows.len(), bad.len(), count("(1)"), count("(2) "), count("(2n)"), count("(3)")).unwrap();
        writeln!(text, "   {}", match class {
            "random-clean" => "streams of whole 4096-frame periods, pauses over 4 periods, power factor >= 0",
            "random-aligned" => "the same streams and pauses, any samples (negative products, stuck values, walks)",
            _ => "any stream length, any pause length, any samples",
        }).unwrap();
        writeln!(text, "   {:.1} h of stream, {} period ends compared, {} with the reference limiting, {} reference faults, {} kernel faults ({} kernel only)",
            rows.iter().map(|r| r.stream_s).sum::<f64>() / 3600.0,
            rows.iter().map(|r| r.periods).sum::<u64>(),
            rows.iter().map(|r| r.ref_limited_periods).sum::<u64>(),
            rows.iter().filter(|r| r.rfault.is_some()).count(),
            rows.iter().filter(|r| r.kfault.is_some()).count(),
            rows.iter().filter(|r| r.kfault.is_some() && r.rfault.is_none()).count()).unwrap();
        let fmin = |f: &dyn Fn(&Res) -> f64| rows.iter().filter(|r| r.periods > 0).map(|r| f(r)).fold(f64::INFINITY, f64::min);
        let fmax = |f: &dyn Fn(&Res) -> f64| rows.iter().filter(|r| r.periods > 0).map(|r| f(r)).fold(f64::NEG_INFINITY, f64::max);
        writeln!(text, "   kernel - reference: coil {:+.6} .. {:+.4} C, magnet {:+.6} .. {:+.4} C, volume {:+} .. {:+} steps (without the reference's hysteresis: max {:+}), inside-period {:+}, nolim {}",
            fmin(&|r| r.dcoil.0), fmax(&|r| r.dcoil.1), fmin(&|r| r.dmag.0), fmax(&|r| r.dmag.1),
            rows.iter().filter(|r| r.periods > 0).map(|r| r.dvol.0).min().unwrap_or(0),
            rows.iter().filter(|r| r.periods > 0).map(|r| r.dvol.1).max().unwrap_or(0),
            rows.iter().filter(|r| r.periods > 0).map(|r| r.dvol_nohyst).max().unwrap_or(0),
            rows.iter().filter(|r| r.periods > 0).map(|r| r.inside).max().unwrap_or(0),
            rows.iter().map(|r| r.unlimited).sum::<u64>()).unwrap();
        if !bad.is_empty() {
            writeln!(text, "   worst: coil {:+.4} C in {}, volume {:+} in {}",
                bad.iter().map(|r| r.dcoil.0).fold(f64::INFINITY, f64::min),
                bad.iter().min_by(|a, b| a.dcoil.0.total_cmp(&b.dcoil.0)).unwrap().name,
                bad.iter().map(|r| r.dvol.1).max().unwrap(),
                bad.iter().max_by_key(|r| r.dvol.1).unwrap().name).unwrap();
            writeln!(text, "   the first {} of them:", bad.len().min(12)).unwrap();
            writeln!(text, "{}", HEADER).unwrap();
            for r in bad.iter().take(12) {
                writeln!(text, "{}", row(r)).unwrap();
                for v in &r.viol {
                    writeln!(text, "       {}", v).unwrap();
                }
            }
        }
        failed += bad.len();
        writeln!(text).unwrap();
    }

    let named: Vec<&Res> = results.iter().filter(|r| r.periods > 0).collect();
    if !named.is_empty() {
        let good: Vec<&&Res> = named.iter().filter(|r| !r.viol.iter().any(|v| v.starts_with("(1)"))).collect();
        writeln!(text, "smallest margin kernel - reference where (1) holds ({} scenarios): coil {:+.6} C, magnet {:+.6} C",
            good.len(),
            good.iter().map(|r| r.dcoil.0).fold(f64::INFINITY, f64::min),
            good.iter().map(|r| r.dmag.0).fold(f64::INFINITY, f64::min)).unwrap();
        writeln!(text, "kernel volume minus the value the reference's temperatures ask for without its hysteresis: max {:+} steps",
            named.iter().map(|r| r.dvol_nohyst).max().unwrap()).unwrap();
        writeln!(text, "overall kernel - reference: coil {:+.4} .. {:+.4} C, magnet {:+.4} .. {:+.4} C, volume {:+} .. {:+} steps",
            named.iter().map(|r| r.dcoil.0).fold(f64::INFINITY, f64::min),
            named.iter().map(|r| r.dcoil.1).fold(f64::NEG_INFINITY, f64::max),
            named.iter().map(|r| r.dmag.0).fold(f64::INFINITY, f64::min),
            named.iter().map(|r| r.dmag.1).fold(f64::NEG_INFINITY, f64::max),
            named.iter().map(|r| r.dvol.0).min().unwrap(),
            named.iter().map(|r| r.dvol.1).max().unwrap()).unwrap();
    }
    writeln!(text, "{} scenarios, {} with violations", results.len(), failed).unwrap();
    writeln!(text, "{}", if failed == 0 { "VERDICT: never less conservative in all tests" } else { "VERDICT: counterexamples found (see the FAIL rows)" }).unwrap();

    print!("{}", text);
    if !out.is_empty() {
        std::fs::write(&out, &text).unwrap();
    }
    if failed != 0 {
        std::process::exit(1);
    }
}
