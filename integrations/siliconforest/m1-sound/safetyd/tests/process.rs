// SPDX-License-Identifier: MIT
// Host tests of the whole daemon as a process (x86_64 Linux; harness = false).
//
// The parent starts this same executable once per scenario with SSD_SCENARIO set; the
// child then IS the daemon: upstream's main loop (daemon::run() of src/main.rs) with
// the fake device of tests/common/fake.rs, which writes a trace of every device
// operation to SSD_TRACE. The parent checks how the process ended and what it did to
// the device before it ended. Run with run-host-tests.sh.

#[path = "../src/main.rs"]
#[allow(dead_code)]
mod daemon;
#[path = "common/fake.rs"]
mod fake;
#[path = "common/synth.rs"]
mod synth;

use std::path::{Path, PathBuf};
use std::process::Command;

use clap::Parser;

const CONF_DIR: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/conf");
const CONF: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/conf/apple/j293.conf");

fn trajectory(n: usize) -> Option<Vec<i16>> {
    if n < synth::total_periods() {
        Some(synth::period(n))
    } else {
        None
    }
}

/// 2 V / 0.2 A for `len` periods
fn quiet(len: usize) -> Box<dyn Fn(usize) -> Option<Vec<i16>>> {
    Box::new(move |n| if n < len { Some(synth::tone(n, (2.0, 0.2))) } else { None })
}

fn script(scenario: &str) -> fake::Script {
    let mut s = fake::Script::new(quiet(100));
    match scenario {
        "trajectory_cold" | "trajectory_warm" | "max_reduction" => s.data = Box::new(trajectory),
        "ping_etimedout" => s.ping_fail = Some((50, libc::ETIMEDOUT)),
        "first_ping_etimedout" => s.ping_fail = Some((1, libc::ETIMEDOUT)),
        "lease_lost" => {
            s.data = Box::new(trajectory);
            s.lease_lost = Some(300);
        }
        "read_error" => s.read_fail = Some((20, libc::EIO)),
        "read_eof" => s.read_eof = Some(20),
        "tripped" => s.trip = Some(20),
        "tripped_at_start" => s.trip = Some(0),
        "bad_version" => s.version = 2,
        "zero_rate" => s.sample_rate = 0,
        "negative_power" => {
            s.data = Box::new(|n| {
                if n < 10 {
                    Some(synth::tone(n, (2.0, 0.2)))
                } else {
                    /* +2 V with -0.2 A on every speaker: -0.4 W */
                    Some([-1748i16, 4681].repeat(synth::PERIOD * synth::SPEAKERS))
                }
            })
        }
        "overheat" => s.data = Box::new(|n| Some(synth::tone(n, (7.746, 0.7746)))),
        "sigquit" => s.sigquit = Some(20),
        "stall" => s.stall = Some((20, 600)),
        _ => panic!("unknown scenario {}", scenario),
    }
    s
}

fn child(scenario: &str) {
    let trace = PathBuf::from(std::env::var("SSD_TRACE").unwrap());
    let script = script(scenario);
    daemon::run(daemon::Options::parse(), move |_| fake::FakeDevice::new(script, Some(trace)));
}

struct Outcome {
    code: Option<i32>,
    stdout: String,
    stderr: String,
    trace: Vec<String>,
}

fn run(scenario: &str, dir: &Path, runtime: &str, extra: &[&str]) -> Outcome {
    let trace = dir.join(format!("{}.trace", scenario));
    let runtime = dir.join(runtime);
    let out = Command::new(std::env::current_exe().unwrap())
        .args(["-c", CONF_DIR, "--runtime-dir", runtime.to_str().unwrap()])
        .args(extra)
        .env("SSD_SCENARIO", scenario)
        .env("SSD_TRACE", &trace)
        .output()
        .unwrap();
    Outcome {
        code: out.status.code(),
        stdout: String::from_utf8_lossy(&out.stdout).to_string(),
        stderr: String::from_utf8_lossy(&out.stderr).to_string(),
        trace: std::fs::read_to_string(&trace)
            .unwrap_or_default()
            .lines()
            .map(|l| l.to_string())
            .collect(),
    }
}

/**
    What must hold in every run, whatever the fault:
    - the process ends like an upstream panic: exit status 101, "Panic!" logged, the
      device closed (which is what locks the volumes in the kernel);
    - no volume is raised unless the lease is held (a good ping, none failed since);
    - in each period the order is upstream's: read, then the volume writes, then one
      ping; a period that completes has exactly one good ping, after its volume writes;
    - after the first failure (ping error, read error or EOF, tripped, lost lease)
      there is no further good ping and no volume is raised.
*/
fn invariants(o: &Outcome) -> Result<(), String> {
    if o.code != Some(101) {
        return Err(format!("exit status {:?}, expected 101", o.code));
    }
    if !o.stdout.contains("Panic!") {
        return Err("no Panic! in the log".to_string());
    }
    if o.trace.first().map(|l| l.as_str()) != Some("open") || o.trace.last().map(|l| l.as_str()) != Some("close") {
        return Err("trace does not start with open and end with close".to_string());
    }

    let mut unlocked = false;
    let mut failed = false;
    let mut in_force = [1u32; 4];
    let mut pings_in_period = 0;
    let mut periods = 0;
    for (i, l) in o.trace.iter().enumerate() {
        let at = |m: &str| Err(format!("trace line {} ({}): {}", i + 1, l, m));
        if l == "ping ok" {
            if failed {
                return at("good ping after a failure");
            }
            unlocked = true;
            pings_in_period += 1;
            if pings_in_period > 1 {
                return at("second ping in one period");
            }
        } else if l.starts_with("ping err") || l == "lease lost" {
            unlocked = false;
            failed = true;
            in_force = [1; 4];
        } else if l.starts_with("read err") || l == "read eof" || l == "info tripped=1" {
            failed = true;
        } else if l == "read" {
            if periods > 0 && pings_in_period != 1 {
                return at("a period ended without its ping");
            }
            periods += 1;
            pings_in_period = 0;
        } else if let Some(rest) = l.strip_prefix("vol ") {
            let (vals, status) = rest.split_once(' ').unwrap();
            let vals: Vec<u32> = vals.split(',').map(|v| v.parse().unwrap()).collect();
            if status != "ok" {
                return at("the kernel refused a volume");
            }
            if periods > 0 && pings_in_period != 0 {
                return at("volume written after the period's ping");
            }
            for (v, f) in vals.iter().zip(in_force.iter_mut()) {
                if *v > *f && (!unlocked || failed) {
                    return at("volume raised without the lease");
                }
                *f = *v;
            }
        }
    }
    Ok(())
}

fn count(o: &Outcome, prefix: &str) -> usize {
    o.trace.iter().filter(|l| l.starts_with(prefix)).count()
}

fn expect(cond: bool, what: &str) -> Result<(), String> {
    if cond {
        Ok(())
    } else {
        Err(what.to_string())
    }
}

fn after<'a>(o: &'a Outcome, line: &str) -> Vec<&'a str> {
    let pos = o.trace.iter().position(|l| l.starts_with(line)).unwrap_or(o.trace.len());
    o.trace[(pos + 1).min(o.trace.len())..]
        .iter()
        .map(|l| l.as_str())
        .filter(|l| !l.starts_with("info"))
        .collect()
}

fn panic_line(o: &Outcome) -> String {
    let lines: Vec<&str> = o.stderr.lines().collect();
    match lines.iter().position(|l| l.contains("panicked at")) {
        Some(i) => lines.get(i + 1).unwrap_or(&"").chars().take(110).collect(),
        None => "(no panic message)".to_string(),
    }
}

fn check_trajectory(o: &Outcome, mode: &str) -> Result<(), String> {
    let reference: Vec<String> = fake::upstream_ref(CONF, mode, "noskip")
        .into_iter()
        .filter(|l| l.starts_with("T "))
        .collect();
    /* the daemon dies in the read after the last period */
    let got = fake::normalize(&o.trace);
    expect(got.len() == reference.len(), "number of device operations differs from upstream")?;
    for (i, (r, g)) in reference.iter().zip(got.iter()).enumerate() {
        if r != g {
            return Err(format!("operation {}: upstream {:?}, port {:?}", i, r, g));
        }
    }
    expect(o.stderr.contains("code: 5"), "did not end with the EIO at the end of the data")
}

fn main() {
    if let Ok(scenario) = std::env::var("SSD_SCENARIO") {
        child(&scenario);
        unreachable!();
    }

    let dir = std::env::temp_dir().join(format!("speakersafetyd-test-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let etimedout = format!("code: {}, kind: TimedOut", libc::ETIMEDOUT);

    type Check = Box<dyn Fn(&Outcome) -> Result<(), String>>;
    let has = |text: &'static str| -> Check { Box::new(move |o: &Outcome| expect(o.stderr.contains(text), text)) };

    /* (name, runtime dir, extra arguments, checks) */
    let scenarios: Vec<(&str, &str, Vec<&str>, Vec<Check>)> = vec![
        ("trajectory_cold", "rt-shared", vec!["--machine", "j293"], vec![
            Box::new(|o| check_trajectory(o, "cold")),
            Box::new(|o| expect(o.stdout.contains("Startup mode: Cold boot"), "cold boot")),
            Box::new(|o| expect(o.stdout.contains("Config file: ") && o.stdout.contains("/conf/apple/j293.conf"), "config file")),
            Box::new(|o| expect(o.stdout.matches("  Amp gain: 18.5 dBV").count() == 4, "amp gain 18.5 dBV")),
            Box::new(|o| expect(o.stdout.matches("  Max power: 0.92 W").count() == 4, "max power 0.92 W")),
            Box::new(|o| expect(o.stdout.matches("  Peak power: 14.596821 W").count() == 4, "peak power 14.596821 W")),
            Box::new(|o| expect(o.stdout.matches("  Min gain: -12.03 dB").count() == 4, "min gain -12.03 dB")),
            Box::new(|o| expect(o.stdout.contains("gain limited to -10.98 dBFS"), "lowest gain -10.98 dBFS")),
        ]),
        /* same runtime directory: the flag file of the run above makes this one warm */
        ("trajectory_warm", "rt-shared", vec!["--machine", "apple,j293"], vec![
            Box::new(|o| check_trajectory(o, "warm")),
            Box::new(|o| expect(o.stdout.contains("Startup mode: Warm boot"), "warm boot")),
            Box::new(|o| expect(o.stdout.contains("gain limited to -14.03 dBFS"), "lowest gain -14.03 dBFS")),
        ]),
        ("ping_etimedout", "rt-a", vec!["--machine", "j293"], vec![
            has("Could not write elem value Speaker Volume Unlock"),
            Box::new({ let e = etimedout.clone(); move |o| expect(o.stderr.contains(&e), "ETIMEDOUT") }),
            Box::new(|o| expect(count(o, "ping ok") == 49 && count(o, "read") == 49, "49 good pings, then the bad one")),
            Box::new(|o| expect(after(o, "ping err") == ["close"], "nothing but close after the failed ping")),
        ]),
        ("first_ping_etimedout", "rt-b", vec!["--machine", "j293"], vec![
            has("Could not write elem value Speaker Volume Unlock"),
            Box::new(|o| expect(count(o, "vol") == 0 && count(o, "read") == 0, "no volume write, no read")),
            Box::new(|o| expect(after(o, "ping err") == ["close"], "nothing but close after the failed ping")),
        ]),
        ("lease_lost", "rt-c", vec!["--machine", "j293"], vec![
            has("Could not write elem value Speaker Volume Unlock"),
            Box::new({ let e = etimedout.clone(); move |o| expect(o.stderr.contains(&e), "ETIMEDOUT") }),
            Box::new(|o| expect(
                after(o, "lease lost") == ["read", "vol 1,1,1,1 ok", "vol 1,1,1,1 ok", "vol 1,1,1,1 ok", "vol 1,1,1,1 ok", "ping err 110", "close"],
                "after the lease is lost: volumes only at the locked value, then the failed ping")),
        ]),
        ("read_error", "rt-d", vec!["--machine", "j293"], vec![
            has("code: 5"),
            Box::new(|o| expect(count(o, "ping ok") == 21, "initial ping + 20 periods")),
            Box::new(|o| expect(after(o, "read err") == ["close"], "nothing but close after the read error")),
        ]),
        ("read_eof", "rt-e", vec!["--machine", "j293"], vec![
            has("end of sense stream"),
            Box::new(|o| expect(after(o, "read eof") == ["close"], "nothing but close after EOF")),
        ]),
        ("tripped", "rt-f", vec!["--machine", "j293"], vec![
            has("the kernel shut the amplifiers down (tripped = 1)"),
            Box::new(|o| expect(count(o, "ping ok") == 21, "initial ping + 20 periods")),
            Box::new(|o| expect(after(o, "info tripped=1") == ["close"], "nothing but close after tripped")),
        ]),
        ("tripped_at_start", "rt-g", vec!["--machine", "j293"], vec![
            has("the kernel shut the amplifiers down (tripped = 1)"),
            Box::new(|o| expect(count(o, "ping") == 0 && count(o, "vol") == 0, "no ping, no volume write")),
        ]),
        ("bad_version", "rt-h", vec!["--machine", "j293"], vec![
            has("unknown device version 2"),
            Box::new(|o| expect(count(o, "ping") == 0 && count(o, "vol") == 0, "no ping, no volume write")),
        ]),
        ("zero_rate", "rt-i", vec!["--machine", "j293"], vec![
            has("Invalid sample rate"),
            Box::new(|o| expect(count(o, "ping ok") == 1 && count(o, "read") == 1, "only the initial ping")),
        ]),
        ("negative_power", "rt-j", vec!["--machine", "j293"], vec![
            has("Left Front: Negative power, bad ivsense data? (-0.4"),
            Box::new(|o| expect(count(o, "ping ok") == 11 && count(o, "read") == 11, "no ping in the bad period")),
        ]),
        ("overheat", "rt-k", vec!["--machine", "j293"], vec![
            has("Left Front: Coil temperature limit exceeded (140.0"),
            Box::new(|o| expect(
                o.trace.iter().rev().filter(|l| !l.starts_with("info")).nth(1).map(|l| l.as_str()) == Some("read"),
                "died in the model, before the period's volume writes and ping")),
        ]),
        ("sigquit", "rt-l", vec!["--machine", "j293"], vec![
            has("SIGQUIT received"),
            Box::new(|o| expect(count(o, "ping ok") == 21, "initial ping + 20 periods")),
            Box::new(|o| expect(after(o, "read err") == ["close"], "nothing but close after the signal")),
        ]),
        ("max_reduction", "rt-m", vec!["--machine", "j293", "-m", "7"], vec![
            has("Gain reduction exceeded threshold"),
            Box::new(|o| expect(o.stdout.contains("gain limited to -7.0") && !o.stdout.contains("gain limited to -7.1"), "stopped at the first gain below -7 dB")),
        ]),
        ("stall", "rt-n", vec!["--machine", "j293", "-v"], vec![
            has("code: 5"),
            Box::new(|o| expect(o.stdout.matches("Skipping 0.").count() == 1, "one catch-up for the stalled read")),
            Box::new(|o| expect(o.stdout.matches("SKIP: Coil").count() == 4, "skip_model for each speaker")),
        ]),
    ];

    let mut failures = 0;
    for (name, runtime, extra, checks) in &scenarios {
        let o = run(name, &dir, runtime, extra);
        let mut errors: Vec<String> = Vec::new();
        if let Err(e) = invariants(&o) {
            errors.push(e);
        }
        for check in checks {
            if let Err(e) = check(&o) {
                errors.push(e);
            }
        }
        println!(
            "{} {:<22} exit {:?}, {} reads, {} good pings, {} volume writes; {}",
            if errors.is_empty() { "ok  " } else { "FAIL" },
            name,
            o.code,
            count(&o, "read") - count(&o, "read e"),
            count(&o, "ping ok"),
            count(&o, "vol"),
            panic_line(&o)
        );
        for e in &errors {
            println!("       {}", e);
        }
        if !errors.is_empty() {
            failures += 1;
            println!("--- stdout (tail)\n{}", o.stdout.lines().rev().take(15).collect::<Vec<_>>().into_iter().rev().collect::<Vec<_>>().join("\n"));
            println!("--- stderr\n{}", o.stderr);
            println!("--- trace (tail)\n{}", o.trace.iter().rev().take(15).rev().cloned().collect::<Vec<_>>().join("\n"));
        }
    }

    let _ = std::fs::remove_dir_all(&dir);
    println!("process tests: {} scenarios, {} failed", scenarios.len(), failures);
    if failures != 0 {
        std::process::exit(1);
    }
}
