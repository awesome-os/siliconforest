// SPDX-License-Identifier: MIT
// (C) 2022 The Asahi Linux Contributors
/*!
    Handles speaker safety on Apple Silicon machines. This code is designed to
    fail safe. The kernel keeps the speakers capped at a low volume level until
    this daemon initializes. If at any time we run into an unrecoverable error
    or a timeout, we panic and let the kernel put the speakers back into a safe
    state.
*/
// Darwin port (SiliconForest, J293) of speakersafetyd 2.0.1 (4175df66).
// Changed against upstream main.rs:
// - The ALSA control device and the sense PCM are one kernel device,
//   /dev/sfspeaker-safety (device.rs). "Speaker Sample Rate" is read with
//   SFSPK_IOC_INFO, "Speaker Volume Unlock" is written with SFSPK_IOC_PING, the sense
//   data is read() from the device. The ESTRPIPE (suspend) branch of the read is
//   gone: the device has no such state and Darwin no such errno.
// - main() is split in two: main() parses the options and run() is the rest of
//   upstream's main(), in upstream's order, generic over the device so that the host
//   tests can run it with a fake one.
// - The machine comes from --machine or sysctl hw.target / hw.model instead of
//   /proc/device-tree/compatible (which is still read on Linux).
// - The flag file is in /var/run/speakersafetyd (option --runtime-dir) instead of
//   systemd's /run/speakersafetyd, and the directory is created if it is missing.
// - clap-verbosity-flag, simple_logger and configparser are replaced by logger.rs
//   and ini.rs.
// - The modules are `pub` (tests/*.rs include this file as a module).
use std::collections::BTreeMap;
use std::fs;
use std::panic::{catch_unwind, resume_unwind, AssertUnwindSafe};
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Instant;

use clap::Parser;
use ini::Ini;
use log::{debug, info, warn};
use logger::{SimpleLogger, Verbosity};

pub mod device;
pub mod helpers;
pub mod ini;
pub mod logger;
pub mod types;
pub mod uclamp;

const DEFAULT_CONFIG_PATH: &str = "share/speakersafetyd";

const UNLOCK_MAGIC: i32 = 0xdec1be15u32 as i32;

const DEFAULT_RUNTIME_DIR: &str = "/var/run/speakersafetyd";
const FLAGFILE: &str = "speakersafetyd.flag";

/// Speaker protection daemon
#[derive(Parser, Debug)]
#[command(version, about, long_about = None)]
pub struct Options {
    /// Path to the configuration file base directory
    #[arg(short, long)]
    config_path: Option<PathBuf>,

    /// Increase the log level
    #[command(flatten)]
    verbose: Verbosity,

    /// Maximum gain reduction before panicing (for debugging)
    #[arg(short, long)]
    max_reduction: Option<f32>,

    /// Machine, as in the device tree: "apple,j293" or "j293" (default: from sysctl)
    #[arg(long)]
    machine: Option<String>,

    /// Directory of the flag file that tells a warm start from a cold one
    #[arg(long)]
    runtime_dir: Option<PathBuf>,
}

#[cfg(target_os = "linux")]
fn get_machine_compatible() -> String {
    fs::read_to_string("/proc/device-tree/compatible")
        .expect("Could not read device tree compatible")
        .split_once("\0")
        .expect("Unexpected compatible format")
        .0
        .to_string()
}

#[cfg(not(target_os = "linux"))]
fn sysctl_string(name: &str) -> Option<String> {
    let cname = std::ffi::CString::new(name).unwrap();
    let mut buf = [0u8; 128];
    let mut len: libc::size_t = buf.len();
    let ret = unsafe {
        libc::sysctlbyname(
            cname.as_ptr(),
            buf.as_mut_ptr() as *mut libc::c_void,
            &mut len,
            core::ptr::null_mut(),
            0,
        )
    };
    if ret != 0 {
        return None;
    }
    let val = &buf[..len.min(buf.len())];
    let val = val.split(|c| *c == 0).next().unwrap();
    let val = String::from_utf8_lossy(val).to_string();
    if val.is_empty() {
        None
    } else {
        Some(val)
    }
}

/// What the device tree's compatible would say, from hw.target ("J293" or "J293AP")
/// or else from hw.model ("MacBookPro17,1", or the target name again).
#[cfg(not(target_os = "linux"))]
fn get_machine_compatible() -> String {
    for name in ["hw.target", "hw.model"] {
        let val = match sysctl_string(name) {
            Some(val) => val,
            None => continue,
        };
        if val == "MacBookPro17,1" {
            return "apple,j293".to_string();
        }
        let val = val.to_ascii_lowercase();
        let digits = val.trim_end_matches(|c: char| c.is_ascii_alphabetic());
        if val.starts_with('j') && digits.len() > 1 && digits[1..].chars().all(|c| c.is_ascii_digit())
        {
            return "apple,".to_string() + &val;
        }
    }
    panic!("Could not tell the machine from sysctl hw.target or hw.model, use --machine");
}

fn get_machine(option: &Option<String>) -> String {
    match option {
        Some(machine) if machine.contains(',') => machine.clone(),
        Some(machine) => "apple,".to_string() + machine,
        None => get_machine_compatible(),
    }
    .trim_end_matches(|c: char| c.is_ascii_alphabetic())
    .to_string()
}

fn get_speakers(config: &Ini) -> Vec<String> {
    config
        .sections()
        .iter()
        .filter_map(|a| a.strip_prefix("Speaker/"))
        .map(|a| a.to_string())
        .collect()
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

#[allow(dead_code)]
fn main() {
    let args = Options::parse();

    run(args, |device| helpers::open_card(device));
}

pub fn run<D, F>(args: Options, open_card: F)
where
    D: device::SpeakerDevice,
    F: FnOnce(&str) -> D,
{
    let sigquit = Arc::new(AtomicBool::new(false));
    signal_hook::flag::register(signal_hook::consts::SIGQUIT, Arc::clone(&sigquit)).unwrap();
    // signal_hook insists on using SA_RESTART, which we don't want. Override it.
    unsafe {
        let mut act: libc::sigaction = core::mem::zeroed();
        assert!(libc::sigaction(signal_hook::consts::SIGQUIT, core::ptr::null(), &mut act) == 0);
        act.sa_flags &= !libc::SA_RESTART;
        assert!(
            libc::sigaction(
                signal_hook::consts::SIGQUIT,
                &mut act,
                core::ptr::null_mut()
            ) == 0
        );
    }

    SimpleLogger::new()
        .with_level(args.verbose.log_level_filter())
        .init()
        .unwrap();
    info!("Starting up");

    let mut config_path = args.config_path.unwrap_or_else(|| {
        let mut path = PathBuf::new();
        path.push(option_env!("PREFIX").unwrap_or("/usr/local"));
        path.push(DEFAULT_CONFIG_PATH);
        path
    });
    info!("Config base: {:?}", config_path);

    let machine: String = get_machine(&args.machine);
    info!("Machine: {}", machine);

    let (maker, model) = machine
        .split_once(",")
        .expect("Unexpected machine name format");

    config_path.push(maker);
    config_path.push(model);
    config_path.set_extension("conf");
    info!("Config file: {:?}", config_path);

    let device = device::SFSPK_DEVICE.to_string();
    info!("Device: {}", device);

    let mut cfg: Ini = Ini::new_cs();
    cfg.load(config_path).expect("Failed to read config file");

    let globals = types::Globals::parse(&cfg);

    if globals.uclamp_min.is_some() || globals.uclamp_max.is_some() {
        uclamp::set_uclamp(
            globals.uclamp_min.unwrap_or(0).try_into().unwrap(),
            globals.uclamp_max.unwrap_or(1024).try_into().unwrap(),
        );
    }

    let runtime_dir = args
        .runtime_dir
        .unwrap_or_else(|| PathBuf::from(DEFAULT_RUNTIME_DIR));
    let max_reduction = args.max_reduction;

    let result = catch_unwind(AssertUnwindSafe(move || {
        let speaker_names = get_speakers(&cfg);
        let speaker_count = speaker_names.len();
        info!("Found {} speakers", speaker_count);

        info!("Opening control device");
        let ctl: D = open_card(&device);

        // systemd makes the runtime directory on Linux; nothing does here.
        let _ = fs::create_dir_all(&runtime_dir);
        let flag_path = runtime_dir.join(FLAGFILE);
        let flag_path = flag_path.as_path();

        let cold_boot = match flag_path.try_exists() {
            Ok(true) => {
                info!("Startup mode: Warm boot");
                false
            }
            Ok(false) => {
                info!("Startup mode: Cold boot");
                if fs::write(flag_path, b"started").is_err() {
                    warn!("Failed to write flag file, continuing as warm boot");
                    false
                } else {
                    true
                }
            }
            Err(_) => {
                warn!("Failed to test flag file, continuing as warm boot");
                false
            }
        };

        let mut groups: BTreeMap<usize, SpeakerGroup> = BTreeMap::new();

        for i in speaker_names {
            let speaker: types::Speaker = types::Speaker::new(&globals, &i, &cfg, &ctl, cold_boot);

            groups
                .entry(speaker.group)
                .or_default()
                .speakers
                .push(speaker);
        }

        assert!(
            groups
                .values()
                .map(|a| a.speakers.len())
                .sum::<usize>()
                == speaker_count
        );
        assert!(2 * speaker_count <= globals.channels);

        // The sense stream comes from the control device, there is no PCM to open
        helpers::check_pcm(globals.channels);

        const SAMPLE_RATE_ELEM: &str = "Speaker Sample Rate";
        let mut sample_rate = helpers::read_info(&ctl, SAMPLE_RATE_ELEM).sample_rate as i32;

        const UNLOCK_ELEM: &str = "Speaker Volume Unlock";

        helpers::write_unlock(&ctl, UNLOCK_MAGIC, UNLOCK_ELEM);

        for (_idx, group) in groups.iter_mut() {
            if cold_boot {
                // Preset the gains to no reduction on cold boot
                group.speakers.iter_mut().for_each(|s| s.update(&ctl, 0.0));
                group.gain = 0.0;
            } else {
                // Leave the gains at whatever the kernel limit is, use anything
                // random for group.gain so the gains will update on the first cycle.
                group.gain = -999.0;
            }
        }

        let mut last_update = Instant::now();

        let mut buf = Vec::new();
        buf.resize(globals.period * globals.channels, 0i16);
        let mut bytes: Vec<u8> = Vec::new();

        let mut once_nominal = false;

        loop {
            if sigquit.load(Ordering::Relaxed) {
                panic!("SIGQUIT received");
            }
            // Block while we're reading into the buffer
            let read = helpers::read_sense(&ctl, &mut bytes, &mut buf, globals.channels);

            let read = match read {
                Ok(a) => Ok(a),
                Err(e) => {
                    if sigquit.load(Ordering::Relaxed) {
                        panic!("SIGQUIT received");
                    }
                    Err(e)
                }
            }
            .unwrap();

            if read != globals.period {
                warn!("Expected {} samples, got {}", globals.period, read);
            }

            if sigquit.load(Ordering::Relaxed) {
                panic!("SIGQUIT received");
            }

            let buf_read = &buf[0..read * globals.channels];

            let cur_sample_rate = helpers::read_info(&ctl, SAMPLE_RATE_ELEM).sample_rate as i32;

            if cur_sample_rate != 0 && cur_sample_rate != sample_rate {
                sample_rate = cur_sample_rate;
                info!("Sample rate: {}", sample_rate);
            }

            if sample_rate == 0 {
                panic!("Invalid sample rate");
            }

            let now = Instant::now();
            let dt = (now - last_update).as_secs_f64();
            assert!(dt > 0f64);

            let pt = globals.period as f64 / sample_rate as f64;
            /* If we skipped at least 4 periods, run catchup for that minus one */
            if dt > (4f64 * pt) {
                let skip = dt - pt;
                debug!("Skipping {:.2} seconds", skip);
                for (_, group) in groups.iter_mut() {
                    group.speakers.iter_mut().for_each(|s| s.skip_model(skip));
                }
            }

            last_update = now;

            let mut all_nominal = true;
            for (idx, group) in groups.iter_mut() {
                let gain = group
                    .speakers
                    .iter_mut()
                    .map(|s| s.run_model(buf_read, sample_rate as f32))
                    .reduce(f32::min)
                    .unwrap();
                if gain != group.gain {
                    if gain == 0. {
                        info!("Speaker group {} gain nominal", idx);
                    } else {
                        info!("Speaker group {} gain limited to {:.2} dBFS", idx, gain);
                    }
                    group.speakers.iter_mut().for_each(|s| s.update(&ctl, gain));
                    group.gain = gain;
                }
                if gain != 0. {
                    all_nominal = false;
                }
                if let Some(max_reduction) = max_reduction {
                    if once_nominal && gain < -max_reduction {
                        panic!("Gain reduction exceeded threshold");
                    }
                }
            }

            if all_nominal {
                once_nominal = true;
            }

            helpers::write_unlock(&ctl, UNLOCK_MAGIC, UNLOCK_ELEM);
        }
    }));
    if let Err(e) = result {
        warn!("Panic!");

        resume_unwind(e);
    }
}
