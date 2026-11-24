//! Fixed ENV profile. All inputs are records, never shell commands or HID events.
use serde::{
    de::{self, MapAccess, SeqAccess, Visitor},
    Deserialize, Deserializer,
};
use serde_json::{json, Map, Value};
use std::{
    fmt,
    fs::{self, OpenOptions},
    io::{self, Read},
    net::Ipv4Addr,
    os::unix::{
        ffi::OsStrExt,
        fs::{MetadataExt, OpenOptionsExt},
    },
    path::{Path, PathBuf},
};
pub const PROFILE: &str = "desktop-native-resolver-two-http-v1";
pub const SCRIPT: &str = "/usr/share/darwin-desktop-network/startup.env";
pub const CLIENT: &str = "/usr/bin/darwin-desktop-net-client";
const SCRIPT_BYTES: &[u8] = include_bytes!("../../startup.env");
const PREFIX: &str = "/private/var/run/darwin-desktop-network/session-";
const PROMPT: &[u8] = b"$ \x1b[?2004h";
#[derive(Clone, Debug)]
pub struct Config {
    pub session: String,
    pub instance: String,
    pub slot: u64,
    pub directory: PathBuf,
    pub seconds: u64,
}
fn bad(text: &str) -> io::Error {
    io::Error::other(text)
}
impl Config {
    pub fn derive(path: &Path, instance: &str, seconds: u64) -> io::Result<Self> {
        let text = path.to_str().ok_or_else(|| bad("records ASCII path"))?;
        if !text.is_ascii()
            || !text.starts_with(PREFIX)
            || text.len() != PREFIX.len() + 48 + 1 + 48
            || !super::status::valid_instance(instance)
            || !(20..=120).contains(&seconds)
        {
            return Err(bad("startup identity/lifetime"));
        }
        let tail = &text[PREFIX.len()..];
        let session = &tail[..48];
        if tail.as_bytes()[48] != b'/'
            || &tail[49..] != instance
            || !super::status::valid_instance(session)
            || instance[4..] != session[..44]
        {
            return Err(bad("startup session/instance"));
        }
        let slot = match &instance[..4] {
            "0001" => 1,
            "0002" => 2,
            _ => return Err(bad("startup slot")),
        };
        Ok(Self {
            session: session.into(),
            instance: instance.into(),
            slot,
            directory: path.into(),
            seconds: (seconds - 10).min(60),
        })
    }
    pub fn environment(&self) -> Vec<String> {
        vec![
            format!("ENV={SCRIPT}"),
            format!("DNDS_SESSION={}", self.session),
            format!("DNDS_INSTANCE={}", self.instance),
            format!("DNDS_RECORDS={}", self.directory.display()),
            format!("DNDS_SECONDS={}", self.seconds),
        ]
    }
    pub fn preflight(&self) -> io::Result<()> {
        trusted_components(&self.directory, true)?;
        trusted_components(Path::new(SCRIPT), false)?;
        trusted_components(Path::new(CLIENT), false)?;
        let script = read_file(Path::new(SCRIPT), 4096, 0o644)?;
        if script != SCRIPT_BYTES {
            return Err(bad("fixed ENV script bytes"));
        }
        let client = fs::symlink_metadata(CLIENT)?;
        if !client.is_file()
            || client.uid() != 0
            || client.nlink() != 1
            || client.mode() & 0o7777 != 0o755
            || client.len() == 0
            || client.len() > 4 * 1024 * 1024
        {
            return Err(bad("fixed client metadata"));
        }
        for path in [
            PathBuf::from(format!("{SCRIPT}.zwc")),
            self.directory.join("network-client.pid"),
            self.directory.join("network-client.pid.tmp"),
            self.directory.join("network-client.json"),
            self.directory.join("network-client.json.tmp"),
            self.directory.join("network-shell-return.txt"),
            self.directory.join("network-shell-return.txt.tmp"),
        ] {
            match fs::symlink_metadata(path) {
                Err(e) if e.kind() == io::ErrorKind::NotFound => {}
                _ => return Err(bad("startup output/compiled sibling exists")),
            }
        }
        Ok(())
    }
}
fn trusted_components(path: &Path, private: bool) -> io::Result<()> {
    if !path.is_absolute() {
        return Err(bad("absolute trusted path"));
    }
    let mut built = PathBuf::from("/");
    for part in path.components() {
        use std::path::Component;
        match part {
            Component::RootDir => continue,
            Component::Normal(p) => built.push(p),
            _ => return Err(bad("noncanonical trusted path")),
        }
        let m = fs::symlink_metadata(&built)?;
        if m.uid() != 0 || m.mode() & 0o022 != 0 || m.file_type().is_symlink() {
            return Err(bad("untrusted ancestor"));
        }
        if built != path && !m.is_dir() {
            return Err(bad("ancestor is not directory"));
        }
        if private
            && built
                .as_os_str()
                .as_bytes()
                .starts_with(b"/private/var/run/darwin-desktop-network")
            && (!m.is_dir() || m.mode() & 0o7777 != 0o700)
        {
            return Err(bad("private ancestor mode"));
        }
    }
    Ok(())
}
fn same(a: &fs::Metadata, b: &fs::Metadata) -> bool {
    a.dev() == b.dev()
        && a.ino() == b.ino()
        && a.mode() == b.mode()
        && a.uid() == b.uid()
        && a.gid() == b.gid()
        && a.nlink() == b.nlink()
        && a.len() == b.len()
        && a.mtime() == b.mtime()
        && a.mtime_nsec() == b.mtime_nsec()
        && a.ctime() == b.ctime()
        && a.ctime_nsec() == b.ctime_nsec()
}
fn read_file(path: &Path, cap: usize, mode: u32) -> io::Result<Vec<u8>> {
    let mut f = OpenOptions::new()
        .read(true)
        .custom_flags(libc::O_CLOEXEC | libc::O_NOFOLLOW | libc::O_NONBLOCK)
        .open(path)?;
    let a = f.metadata()?;
    if !a.is_file()
        || a.uid() != 0
        || a.nlink() != 1
        || a.mode() & 0o7777 != mode
        || a.len() == 0
        || a.len() > cap as u64
    {
        return Err(bad("private record metadata"));
    }
    let mut data = Vec::new();
    f.by_ref().take(cap as u64 + 1).read_to_end(&mut data)?;
    let b = f.metadata()?;
    if !same(&a, &b) || data.len() != a.len() as usize {
        return Err(bad("private record changed"));
    }
    Ok(data)
}
struct Unique(Value);
impl<'de> Deserialize<'de> for Unique {
    fn deserialize<D: Deserializer<'de>>(d: D) -> Result<Self, D::Error> {
        struct V;
        impl<'de> Visitor<'de> for V {
            type Value = Unique;
            fn expecting(&self, f: &mut fmt::Formatter) -> fmt::Result {
                f.write_str("bounded ASCII integer JSON")
            }
            fn visit_bool<E: de::Error>(self, v: bool) -> Result<Unique, E> {
                Ok(Unique(Value::Bool(v)))
            }
            fn visit_i64<E: de::Error>(self, v: i64) -> Result<Unique, E> {
                Ok(Unique(v.into()))
            }
            fn visit_u64<E: de::Error>(self, v: u64) -> Result<Unique, E> {
                Ok(Unique(v.into()))
            }
            fn visit_unit<E: de::Error>(self) -> Result<Unique, E> {
                Ok(Unique(Value::Null))
            }
            fn visit_str<E: de::Error>(self, v: &str) -> Result<Unique, E> {
                if !v.is_ascii() {
                    return Err(E::custom("nonASCII JSON"));
                }
                Ok(Unique(v.into()))
            }
            fn visit_string<E: de::Error>(self, v: String) -> Result<Unique, E> {
                self.visit_str(&v)
            }
            fn visit_seq<A: SeqAccess<'de>>(self, mut a: A) -> Result<Unique, A::Error> {
                let mut out = Vec::new();
                while let Some(v) = a.next_element::<Unique>()? {
                    if out.len() >= 128 {
                        return Err(de::Error::custom("JSON array cap"));
                    }
                    out.push(v.0);
                }
                Ok(Unique(Value::Array(out)))
            }
            fn visit_map<A: MapAccess<'de>>(self, mut a: A) -> Result<Unique, A::Error> {
                let mut out = Map::new();
                while let Some(key) = a.next_key::<String>()? {
                    if !key.is_ascii() || out.contains_key(&key) || out.len() >= 128 {
                        return Err(de::Error::custom("duplicate/key/object cap"));
                    }
                    let v = a.next_value::<Unique>()?;
                    out.insert(key, v.0);
                }
                Ok(Unique(Value::Object(out)))
            }
        }
        d.deserialize_any(V)
    }
}
fn strict_json(data: &[u8]) -> io::Result<Value> {
    if data.is_empty() || data.len() > 16384 || !data.is_ascii() {
        return Err(bad("client JSON bytes"));
    }
    let mut d = serde_json::Deserializer::from_slice(data);
    let value = Unique::deserialize(&mut d).map_err(|_| bad("client strict JSON"))?;
    d.end().map_err(|_| bad("client JSON trailing"))?;
    Ok(value.0)
}
fn number(v: &Value, key: &str) -> io::Result<u64> {
    v.get(key)
        .and_then(Value::as_u64)
        .ok_or_else(|| bad("client integer field"))
}
fn string<'a>(v: &'a Value, key: &str) -> io::Result<&'a str> {
    v.get(key)
        .and_then(Value::as_str)
        .ok_or_else(|| bad("client string field"))
}
fn truth(v: &Value, key: &str) -> bool {
    v.get(key).and_then(Value::as_bool) == Some(true)
}
fn hex(data: &str, cap: usize) -> io::Result<Vec<u8>> {
    if !data.len().is_multiple_of(2)
        || data.len() > 2 * cap
        || !data
            .bytes()
            .all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b))
    {
        return Err(bad("client lowerhex"));
    }
    (0..data.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&data[i..i + 2], 16).map_err(|_| bad("client hex")))
        .collect()
}
fn request(session: &str) -> Vec<u8> {
    format!("GET / HTTP/1.1\r\nHost: example.com\r\nUser-Agent: darwin-lab-egress/1\r\nAccept-Encoding: identity\r\nX-Darwin-Lab-Nonce: {session}\r\nConnection: close\r\n\r\n").into_bytes()
}
fn canonical_ipv4(s: &str) -> bool {
    s.parse::<Ipv4Addr>().is_ok_and(|ip| ip.to_string() == s)
}
fn validate_client(v: &Value, c: &Config, shell: i32) -> io::Result<i32> {
    let pid = number(v, "pid")?;
    if !(2..=i32::MAX as u64).contains(&pid) || pid == shell as u64 {
        return Err(bad("client PID"));
    }
    if number(v, "version")? != 1
        || string(v, "profile")? != PROFILE
        || string(v, "nonce")? != c.session
        || string(v, "instance")? != c.instance
        || number(v, "slot")? != c.slot
    {
        return Err(bad("client identity"));
    }
    for key in ["ppid", "sid", "pgid", "foreground_pgid"] {
        if number(v, key)? != shell as u64 {
            return Err(bad("client foreground identity"));
        }
    }
    if string(v, "status")? != "PASS"
        || string(v, "error")? != "none"
        || number(v, "error_errno")? != 0
        || v.get("gai_rc").and_then(Value::as_i64) != Some(0)
        || number(v, "system_errno")? != 0
    {
        return Err(bad("client failed"));
    }
    for key in [
        "address_list_complete",
        "entry_fd_check_observed",
        "request_shutdown",
        "peer_eof",
        "socket_created",
        "socket_closed",
    ] {
        if !truth(v, key) {
            return Err(bad("client completion field"));
        }
    }
    if !(3..=65536).contains(&number(v, "entry_fd_scan_bound_exclusive")?)
        || number(v, "tcp_maxseg_request")? != 256
        || !(1..=65535).contains(&number(v, "tcp_maxseg_observed")?)
    {
        return Err(bad("client bounds"));
    }
    let addresses = v
        .get("addresses")
        .and_then(Value::as_array)
        .ok_or_else(|| bad("client addresses"))?;
    if !(1..=32).contains(&addresses.len())
        || !addresses
            .iter()
            .all(|a| a.as_str().is_some_and(canonical_ipv4))
    {
        return Err(bad("client address list"));
    }
    let selected = addresses[0].as_str().unwrap();
    if string(v, "selected")? != selected {
        return Err(bad("client selected"));
    }
    let local = v.get("local").ok_or_else(|| bad("local tuple"))?;
    let peer = v.get("peer").ok_or_else(|| bad("peer tuple"))?;
    if string(local, "address")? != "10.0.2.15"
        || !(1024..=65535).contains(&number(local, "port")?)
        || string(peer, "address")? != selected
        || number(peer, "port")? != 80
    {
        return Err(bad("client socket tuple"));
    }
    let wire = hex(string(v, "request_hex")?, 186)?;
    let response = hex(string(v, "response_hex")?, 2048)?;
    if wire != request(&c.session)
        || number(v, "request_bytes")? != 186
        || response.is_empty()
        || number(v, "response_bytes")? != response.len() as u64
        || !response.starts_with(b"HTTP/1.1 200 ") && !response.starts_with(b"HTTP/1.0 200 ")
    {
        return Err(bad("client HTTP bytes"));
    }
    let http = v.get("http").ok_or_else(|| bad("client HTTP facts"))?;
    if number(http, "status")? != 200
        || !(1..=response.len() as u64).contains(&number(http, "header_bytes")?)
        || number(http, "body_bytes")? > 2048
        || !(1..=3).contains(&number(http, "framing")?)
        || !["framing_complete", "eof_observed", "example_content"]
            .iter()
            .all(|k| truth(http, k))
    {
        return Err(bad("client HTTP facts"));
    }
    if number(v, "started_ms")? == 0 || number(v, "elapsed_ms")? > c.seconds * 1000 {
        return Err(bad("client lifetime"));
    }
    Ok(pid as i32)
}
#[derive(Default)]
pub struct Tracker {
    bytes: Vec<u8>,
    pub prompt_seen: bool,
    marker: Option<(i32, i32, Vec<u8>)>,
    foreground: i32,
    error: Option<&'static str>,
}
impl Tracker {
    pub fn observe(&mut self, data: &[u8], c: &Config, shell: i32) {
        if self.error.is_some() {
            return;
        }
        if self.bytes.len() + data.len() > 8192 {
            if self.prompt_seen {
                return;
            }
            self.error = Some("startup_output_cap");
            return;
        }
        self.bytes.extend_from_slice(data);
        let mut at = 0;
        let mut found = None;
        for line in self.bytes.split_inclusive(|b| *b == b'\n') {
            if line.last() != Some(&b'\n') {
                break;
            }
            let mut text = &line[..line.len() - 1];
            while text.last() == Some(&b'\r') {
                text = &text[..text.len() - 1];
            }
            if text.starts_with(b"DL30_SHELL_RETURN") {
                if found.is_some() {
                    self.error = Some("duplicate_shell_return");
                    return;
                }
                let Ok(text) = std::str::from_utf8(text) else {
                    self.error = Some("shell_return");
                    return;
                };
                let words: Vec<_> = text.split(' ').collect();
                if words.len() != 6 {
                    self.error = Some("shell_return");
                    return;
                }
                let parsed = (|| -> Option<(i32, i32)> {
                    let client = words[4].strip_prefix("client_pid=")?.parse::<i32>().ok()?;
                    let status = words[5].strip_prefix("status=")?.parse::<i32>().ok()?;
                    if client < 2 || client == shell || !(0..=255).contains(&status) {
                        return None;
                    }
                    Some((client, status))
                })();
                let Some((client, status)) = parsed else {
                    self.error = Some("shell_return");
                    return;
                };
                let expected = format!(
                    "DL30_SHELL_RETURN session={} instance={} shell_pid={} client_pid={} status={}",
                    c.session, c.instance, shell, client, status
                );
                if text != expected {
                    self.error = Some("shell_return_identity");
                    return;
                }
                found = Some((
                    client,
                    status,
                    format!("{expected}\n").into_bytes(),
                    at + line.len(),
                ));
            }
            at += line.len();
        }
        if let Some((client, status, line, end)) = found {
            self.prompt_seen = self.bytes[end..].windows(PROMPT.len()).any(|w| w == PROMPT);
            self.marker = Some((client, status, line));
        }
    }
    pub fn needs_foreground(&self) -> bool {
        self.prompt_seen && self.foreground == 0
    }
    pub fn foreground(&mut self, value: i32, shell: i32) {
        self.foreground = value;
        if value != shell {
            self.error = Some("fallback_foreground");
        }
    }
    pub fn finish(&self, c: &Config, shell: i32) -> Value {
        let mut result = json!({"version":1,"profile":PROFILE,"session_nonce":c.session,"instance":c.instance,"slot":c.slot,"shell_pid":shell,"client_pid":0,"client_record_complete":false,"client_success":false,"shell_return_observed":false,"client_exit_status":-1,"client_reaped_by_shell":false,"fallback_prompt_observed":self.prompt_seen,"foreground_pgid":self.foreground,"foreground_matches_shell":self.foreground==shell&&shell>1,"complete":false,"error":"missing_client_record"});
        let evaluated = (|| -> io::Result<()> {
            let data = read_file(&c.directory.join("network-client.json"), 16384, 0o600)?;
            let v = strict_json(&data)?;
            result["client_record_complete"] = json!(true);
            let pid = validate_client(&v, c, shell)?;
            result["client_pid"] = json!(pid);
            result["client_success"] = json!(true);
            let pidfile = read_file(&c.directory.join("network-client.pid"), 32, 0o600)?;
            if pidfile != format!("{pid}\n").as_bytes() {
                return Err(bad("client_pid_file"));
            }
            let returned = read_file(&c.directory.join("network-shell-return.txt"), 512, 0o600)?;
            let Some((seen, status, line)) = &self.marker else {
                return Err(bad("missing_shell_return"));
            };
            result["client_exit_status"] = json!(status);
            if *seen != pid || returned != *line {
                return Err(bad("shell_return_file"));
            }
            result["shell_return_observed"] = json!(true);
            if *status != 0 {
                return Err(bad("client_exit_status"));
            }
            result["client_reaped_by_shell"] = json!(true);
            if let Some(error) = self.error {
                return Err(bad(error));
            }
            if !self.prompt_seen || self.foreground != shell {
                return Err(bad("missing_fallback_prompt_or_foreground"));
            }
            result["complete"] = json!(true);
            result["error"] = json!("none");
            Ok(())
        })();
        if let Err(error) = evaluated {
            result["error"] = json!(if let Some(error) = self.error {
                error
            } else if error.kind() == io::ErrorKind::NotFound {
                "missing_startup_record"
            } else {
                "startup_evidence_failed"
            });
        }
        result
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn config() -> Config {
        let nonce = "a".repeat(48);
        let instance = format!("0001{}", &nonce[..44]);
        Config::derive(
            Path::new(&format!("{PREFIX}{nonce}/{instance}")),
            &instance,
            80,
        )
        .unwrap()
    }
    fn wire_hex(data: &[u8]) -> String {
        data.iter().map(|b| format!("{b:02x}")).collect()
    }
    fn record() -> Value {
        let c = config();
        let response=b"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 18\r\n\r\nExample Domain body";
        json!({"version":1,"profile":PROFILE,"nonce":c.session,"instance":c.instance,"slot":1,"pid":10,"ppid":9,"sid":9,"pgid":9,"foreground_pgid":9,"status":"PASS","error":"none","error_errno":0,"gai_rc":0,"system_errno":0,"address_list_complete":true,"addresses":["172.66.147.243","104.20.23.154"],"selected":"172.66.147.243","local":{"address":"10.0.2.15","port":50001},"peer":{"address":"172.66.147.243","port":80},"entry_fd_check_observed":true,"entry_fd_scan_bound_exclusive":10240,"tcp_maxseg_request":256,"tcp_maxseg_observed":256,"request_hex":wire_hex(&request(&c.session)),"response_hex":wire_hex(response),"request_bytes":186,"response_bytes":response.len(),"request_shutdown":true,"peer_eof":true,"socket_created":true,"socket_closed":true,"http":{"status":200,"header_bytes":60,"body_bytes":19,"framing":1,"framing_complete":true,"eof_observed":true,"example_content":true},"started_ms":1000,"elapsed_ms":3000})
    }
    fn marker(c: &Config) -> Vec<u8> {
        format!(
            "DL30_SHELL_RETURN session={} instance={} shell_pid=9 client_pid=10 status=0\r\n",
            c.session, c.instance
        )
        .into_bytes()
    }
    #[test]
    fn identity_exact_trusted_ancestry_and_env() {
        let c = config();
        assert_eq!(c.slot, 1);
        assert_eq!(c.seconds, 60);
        assert_eq!(c.environment().len(), 5);
        assert_eq!(c.environment()[0], format!("ENV={SCRIPT}"));
        let two = c.instance.replacen("0001", "0002", 1);
        let path = PathBuf::from(format!("{PREFIX}{}/{}", c.session, two));
        assert_eq!(Config::derive(&path, &two, 25).unwrap().seconds, 15);
        for value in [
            format!("{PREFIX}{}/x/{}", c.session, c.instance),
            format!("{PREFIX}{}/../{}", c.session, c.instance),
            format!("{PREFIX}{}/{}", "b".repeat(48), c.instance),
            format!("/tmp/session-{}/{}", c.session, c.instance),
            format!("{PREFIX}{}/{}", "é".repeat(24), c.instance),
        ] {
            assert!(Config::derive(Path::new(&value), &c.instance, 80).is_err());
        }
        for n in [0, 1, 19, 121, u64::MAX] {
            assert!(Config::derive(&c.directory, &c.instance, n).is_err());
        }
        let third = c.instance.replacen("0001", "0003", 1);
        assert!(Config::derive(
            Path::new(&format!("{PREFIX}{}/{}", c.session, third)),
            &third,
            80
        )
        .is_err());
    }
    #[test]
    fn strict_ascii_unique_integer_json() {
        let value = record();
        assert!(validate_client(
            &strict_json(&serde_json::to_vec(&value).unwrap()).unwrap(),
            &config(),
            9
        )
        .is_ok());
        for raw in [
            b"{\"a\":1,\"a\":2}".as_slice(),
            b"{\"a\":{\"b\":1,\"b\":1}}",
            b"{\"a\":1,\"\\u0061\":1}",
            b"{\"a\":1.0}",
            b"{\"a\":1e2}",
            b"{\"a\":\"\\u00e9\"}",
            b"{} garbage",
            b"{\"a\":18446744073709551616}",
        ] {
            assert!(strict_json(raw).is_err(), "{raw:?}");
        }
    }
    #[test]
    fn full_client_record_identity_and_socket_tuple() {
        let base = record();
        assert_eq!(validate_client(&base, &config(), 9).unwrap(), 10);
        for key in [
            "address_list_complete",
            "entry_fd_check_observed",
            "request_shutdown",
            "peer_eof",
            "socket_created",
            "socket_closed",
        ] {
            let mut v = base.clone();
            v[key] = json!(false);
            assert!(validate_client(&v, &config(), 9).is_err(), "{key}");
        }
        for (key, value) in [
            ("version", json!(2)),
            ("slot", json!(2)),
            ("pid", json!(9)),
            ("pid", json!(0)),
            ("pid", json!(2147483648u64)),
            ("ppid", json!(11)),
            ("sid", json!(10)),
            ("pgid", json!(10)),
            ("foreground_pgid", json!(10)),
            ("gai_rc", json!(-1)),
            ("status", json!("FAILED")),
            ("error", json!("gai")),
            ("error_errno", json!(1)),
            ("system_errno", json!(1)),
            ("entry_fd_scan_bound_exclusive", json!(65537)),
            ("tcp_maxseg_observed", json!(0)),
            ("request_bytes", json!(185)),
            ("response_bytes", json!(2049)),
            ("elapsed_ms", json!(60001)),
            ("nonce", json!("b".repeat(48))),
            ("instance", json!("b".repeat(48))),
            ("request_hex", json!("00")),
            ("response_hex", json!("00")),
            ("addresses", json!([])),
            ("selected", json!("104.20.23.154")),
        ] {
            let mut v = base.clone();
            v[key] = value;
            assert!(validate_client(&v, &config(), 9).is_err(), "{key}");
        }
        for (part, key, value) in [
            ("local", "address", json!("127.0.0.1")),
            ("local", "port", json!(80)),
            ("peer", "address", json!("104.20.23.154")),
            ("peer", "port", json!(81)),
            ("http", "status", json!(201)),
            ("http", "eof_observed", json!(false)),
            ("http", "framing_complete", json!(false)),
            ("http", "example_content", json!(false)),
        ] {
            let mut v = base.clone();
            v[part][key] = value;
            assert!(validate_client(&v, &config(), 9).is_err(), "{part}/{key}");
        }
        let mut duplicate = base.clone();
        duplicate["addresses"] = json!(["172.66.147.243", "172.66.147.243"]);
        assert!(validate_client(&duplicate, &config(), 9).is_ok());
    }
    #[test]
    fn every_marker_prompt_split_preserves_real_order() {
        let c = config();
        let mut bytes = b"DL30_CLIENT_BEGIN harmless\r\n".to_vec();
        bytes.extend(marker(&c));
        bytes.extend(b"\x1b[1m#\x1b[m\r\n$ \x1b[?2004h");
        for split in 0..=bytes.len() {
            let mut t = Tracker::default();
            t.observe(&bytes[..split], &c, 9);
            t.observe(&bytes[split..], &c, 9);
            assert!(t.error.is_none(), "{split}");
            assert!(t.prompt_seen);
            assert_eq!(t.marker.as_ref().unwrap().0, 10);
            assert!(t.needs_foreground());
            t.foreground(9, 9);
            assert!(!t.needs_foreground());
            assert_eq!(t.foreground, 9);
        }
    }
    #[test]
    fn prompt_alone_missing_env_or_before_return_never_proves_return() {
        let c = config();
        let mut t = Tracker::default();
        t.observe(PROMPT, &c, 9);
        assert!(!t.prompt_seen && t.marker.is_none());
        let mut data = PROMPT.to_vec();
        data.extend(b"\r\n");
        data.extend(marker(&c));
        let mut t = Tracker::default();
        t.observe(&data, &c, 9);
        assert!(!t.prompt_seen);
        let mut t = Tracker::default();
        let mut data = marker(&c);
        data.extend(b"$ ");
        t.observe(&data, &c, 9);
        assert!(!t.prompt_seen);
    }
    #[test]
    fn marker_session_pid_status_duplicates_and_foreground_fail_closed() {
        let c = config();
        let normal = String::from_utf8(marker(&c)).unwrap();
        for text in [
            normal.replace("shell_pid=9", "shell_pid=10"),
            normal.replace("client_pid=10", "client_pid=9"),
            normal.replace("client_pid=10", "client_pid=010"),
            normal.replace("status=0", "status=256"),
            normal.replace(&c.session, &"b".repeat(48)),
            format!("{normal}{normal}"),
        ] {
            let mut t = Tracker::default();
            t.observe(text.as_bytes(), &c, 9);
            assert!(t.error.is_some(), "{text}");
        }
        let mut t = Tracker::default();
        let mut data = marker(&c);
        data.extend(PROMPT);
        t.observe(&data, &c, 9);
        t.foreground(10, 9);
        assert!(t.error.is_some());
        let mut failure = Tracker::default();
        failure.observe(normal.replace("status=0", "status=1").as_bytes(), &c, 9);
        assert_eq!(failure.marker.as_ref().unwrap().1, 1);
    }
    #[test]
    fn file_stability_metadata_detects_mutation() {
        let path = std::env::temp_dir().join(format!("dnds-stat-{}", std::process::id()));
        fs::write(&path, b"a").unwrap();
        let before = fs::metadata(&path).unwrap();
        fs::write(&path, b"longer").unwrap();
        let after = fs::metadata(&path).unwrap();
        assert!(!same(&before, &after));
        fs::remove_file(path).unwrap();
    }
}
