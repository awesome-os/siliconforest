// SPDX-License-Identifier: MIT
// (C) 2022 The Asahi Linux Contributors
//
// Darwin port: this file is NEW. Upstream uses the simple_logger crate (without its
// default features, i.e. no colours and no timestamps) and clap-verbosity-flag; neither
// is in the lab's offline cargo registry. These are the two small parts the daemon
// uses, with the same output format ("LEVEL [target] message" on stdout) and the same
// flags (-v/--verbose and -q/--quiet, repeatable, starting from the Info level).

use log::{Level, LevelFilter, Log, Metadata, Record, SetLoggerError};

pub struct SimpleLogger {
    level: LevelFilter,
}

impl SimpleLogger {
    #[allow(clippy::new_without_default)]
    pub fn new() -> SimpleLogger {
        SimpleLogger {
            level: LevelFilter::Trace,
        }
    }

    pub fn with_level(mut self, level: LevelFilter) -> SimpleLogger {
        self.level = level;
        self
    }

    pub fn init(self) -> Result<(), SetLoggerError> {
        log::set_max_level(self.level);
        log::set_logger(Box::leak(Box::new(self)))
    }
}

impl Log for SimpleLogger {
    fn enabled(&self, metadata: &Metadata) -> bool {
        metadata.level().to_level_filter() <= self.level
    }

    fn log(&self, record: &Record) {
        if self.enabled(record.metadata()) {
            let target = if !record.target().is_empty() {
                record.target()
            } else {
                record.module_path().unwrap_or_default()
            };
            println!("{:<5} [{}] {}", record.level().to_string(), target, record.args());
        }
    }

    fn flush(&self) {}
}

/// The flags of clap_verbosity_flag::Verbosity<InfoLevel>.
#[derive(clap::Args, Debug, Clone, Default)]
pub struct Verbosity {
    /// Increase logging verbosity
    #[arg(short = 'v', long, action = clap::ArgAction::Count, global = true)]
    verbose: u8,

    /// Decrease logging verbosity
    #[arg(short = 'q', long, action = clap::ArgAction::Count, global = true, conflicts_with = "verbose")]
    quiet: u8,
}

impl Verbosity {
    pub fn log_level_filter(&self) -> LevelFilter {
        /* Off = 0 ... Trace = 5, the default is Info */
        let level = Level::Info as i16 + self.verbose as i16 - self.quiet as i16;
        match level {
            i16::MIN..=0 => LevelFilter::Off,
            1 => LevelFilter::Error,
            2 => LevelFilter::Warn,
            3 => LevelFilter::Info,
            4 => LevelFilter::Debug,
            _ => LevelFilter::Trace,
        }
    }
}
