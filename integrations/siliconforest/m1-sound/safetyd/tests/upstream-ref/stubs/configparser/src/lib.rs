// SPDX-License-Identifier: MIT
// Stand-in for the configparser crate in the host reference build: the port's own
// ini.rs under the crate's name (the config values that come out of it are checked
// against the real daemon's log in tests/model.rs).
#[path = "../../../../../src/ini.rs"]
pub mod ini;
