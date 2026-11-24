// SPDX-License-Identifier: MIT
// (C) 2022 The Asahi Linux Contributors
//
// Darwin port: this file is NEW. Upstream uses the configparser crate (3.x, feature
// "indexmap"), which is not in the lab's offline cargo registry. This is the part of
// its API the daemon uses, with configparser's defaults for Ini::new_cs(): case
// sensitive, delimiters '=' and ':', comment symbols ';' and '#' (anywhere in a
// line), sections in file order, keys before the first section in "default" (which
// sections() does not list), a repeated key keeps the last value.

use std::fs;
use std::path::Path;

#[derive(Default)]
pub struct Ini {
    sections: Vec<(String, Vec<(String, Option<String>)>)>,
}

const DEFAULT_SECTION: &str = "default";

impl Ini {
    pub fn new_cs() -> Ini {
        Default::default()
    }

    pub fn load<T: AsRef<Path>>(&mut self, path: T) -> Result<(), String> {
        let text = fs::read_to_string(&path).map_err(|e| {
            format!("couldn't read {}: {}", path.as_ref().display(), e)
        })?;
        self.read(&text)
    }

    pub fn read(&mut self, text: &str) -> Result<(), String> {
        self.sections.clear();
        let mut section = DEFAULT_SECTION.to_string();

        for (num, line) in text.lines().enumerate() {
            let trimmed = match line.find(|c: char| c == ';' || c == '#') {
                Some(idx) => line[..idx].trim(),
                None => line.trim(),
            };
            if trimmed.is_empty() {
                continue;
            }
            if let Some(rest) = trimmed.strip_prefix('[') {
                match rest.find(']') {
                    Some(end) => {
                        section = rest[..end].to_string();
                        self.section_mut(&section);
                    }
                    None => {
                        return Err(format!(
                            "line {}: Found opening bracket for section name but no closing bracket",
                            num + 1
                        ));
                    }
                }
                continue;
            }
            let (key, value) = match trimmed.find(|c: char| c == '=' || c == ':') {
                Some(idx) => (
                    trimmed[..idx].trim(),
                    Some(trimmed[idx + 1..].trim().to_string()),
                ),
                None => (trimmed, None),
            };
            if key.is_empty() {
                return Err(format!("line {}: Key cannot be empty", num + 1));
            }
            let entries = self.section_mut(&section);
            match entries.iter_mut().find(|(k, _)| k == key) {
                Some(entry) => entry.1 = value,
                None => entries.push((key.to_string(), value)),
            }
        }
        Ok(())
    }

    fn section_mut(&mut self, name: &str) -> &mut Vec<(String, Option<String>)> {
        if let Some(idx) = self.sections.iter().position(|(n, _)| n == name) {
            return &mut self.sections[idx].1;
        }
        self.sections.push((name.to_string(), Vec::new()));
        &mut self.sections.last_mut().unwrap().1
    }

    pub fn sections(&self) -> Vec<String> {
        self.sections
            .iter()
            .map(|(n, _)| n.clone())
            .filter(|n| n != DEFAULT_SECTION)
            .collect()
    }

    pub fn get(&self, section: &str, key: &str) -> Option<String> {
        self.sections
            .iter()
            .find(|(n, _)| n == section)?
            .1
            .iter()
            .find(|(k, _)| k == key)?
            .1
            .clone()
    }

    pub fn getint(&self, section: &str, key: &str) -> Result<Option<i64>, String> {
        match self.get(section, key) {
            Some(val) => val.parse::<i64>().map(Some).map_err(|e| e.to_string()),
            None => Ok(None),
        }
    }

    pub fn getfloat(&self, section: &str, key: &str) -> Result<Option<f64>, String> {
        match self.get(section, key) {
            Some(val) => val.parse::<f64>().map(Some).map_err(|e| e.to_string()),
            None => Ok(None),
        }
    }
}
