//! The memory behind a wl_shm pool.
//!
//! First choice: POSIX shared memory (`shm_open`), which lives in RAM and
//! takes no room on the root file system. The name is removed at once, the
//! size is set once (Darwin allows one `ftruncate` per object), and the
//! pixels are reached through one shared mapping (Darwin's shm descriptors
//! have no `read`/`write`). If any step fails, everything it made is released
//! and the earlier behaviour is used instead: an unlinked file in the records
//! directory, written with seek + write. `CHONK_TERMINAL_SHM=file` in the
//! terminal's own environment asks for the file directly.
//!
//! Every failure names the buffer, the backing, the operation and the errno.
use std::{
    ffi::CString,
    fs::{File, OpenOptions},
    io::{self, Seek, SeekFrom, Write},
    os::{
        fd::{AsFd, AsRawFd, BorrowedFd, FromRawFd, OwnedFd},
        unix::fs::{OpenOptionsExt, PermissionsExt},
    },
    path::Path,
    sync::atomic::{AtomicU32, Ordering},
};

/// Environment variable of the terminal itself (never given to the shell):
/// `file` skips POSIX shared memory. Any other value, or none: try it first.
pub const ENV: &str = "CHONK_TERMINAL_SHM";
/// Darwin's PSHMNAMLEN: the longest name, leading slash included.
#[cfg_attr(not(test), allow(dead_code))]
pub const NAME_MAX: usize = 31;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Kind {
    Posix,
    File,
}
impl Kind {
    pub fn name(self) -> &'static str {
        match self {
            Kind::Posix => "posix",
            Kind::File => "file",
        }
    }
}

/// Which backings may be used.
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct Policy {
    /// Do not try POSIX shared memory.
    pub file_only: bool,
    /// Tests only: the named step of that backing fails with ENOSPC instead
    /// of running.
    #[cfg(test)]
    pub fail_posix: Option<&'static str>,
    #[cfg(test)]
    pub fail_file: Option<&'static str>,
}
impl Policy {
    pub fn from_env(value: Option<&str>) -> Self {
        Self { file_only: value == Some("file"), ..Self::default() }
    }
    fn fail_posix(&self) -> Option<&'static str> {
        #[cfg(test)]
        return self.fail_posix;
        #[cfg(not(test))]
        None
    }
    fn fail_file(&self) -> Option<&'static str> {
        #[cfg(test)]
        return self.fail_file;
        #[cfg(not(test))]
        None
    }
}
#[cfg(test)]
thread_local!(pub static TEST_POLICY: std::cell::Cell<Option<Policy>> = const { std::cell::Cell::new(None) });
/// The policy of this process: from its environment.
pub fn policy() -> Policy {
    #[cfg(test)]
    if let Some(policy) = TEST_POLICY.with(|p| p.get()) {
        return policy;
    }
    Policy::from_env(std::env::var(ENV).ok().as_deref())
}

/// One number per shared-memory object of this process.
static NEXT: AtomicU32 = AtomicU32::new(1);
/// `/cht-<pid>-<n>`: at most 5 + 10 + 1 + 10 = 26 bytes.
pub fn name(pid: u32, n: u32) -> String {
    format!("/cht-{pid}-{n}")
}

fn errno(e: &io::Error) -> String {
    match e.raw_os_error() {
        Some(n) => format!("errno {n}"),
        None => e.to_string(),
    }
}
/// Run one named step; its failure becomes "<step>: errno <n>".
fn step<T>(fail: Option<&str>, op: &str, run: impl FnOnce() -> io::Result<T>) -> Result<T, String> {
    if fail == Some(op) {
        return Err(format!("{op}: errno {}", libc::ENOSPC));
    }
    run().map_err(|e| format!("{op}: {}", errno(&e)))
}
fn check(r: libc::c_int) -> io::Result<libc::c_int> {
    if r < 0 {
        Err(io::Error::last_os_error())
    } else {
        Ok(r)
    }
}

enum Store {
    /// The descriptor, and its one shared read/write mapping of `len` bytes.
    Posix { fd: OwnedFd, map: *mut u8, len: usize },
    File(File),
}
pub struct Backing {
    store: Store,
    /// "shm buffer WxH (N bytes)", the start of every error.
    what: String,
    /// The shared-memory name that was used (and removed at once).
    name: Option<String>,
    /// Why POSIX shared memory was not used, when it was tried.
    fallback: Option<String>,
}

fn posix(bytes: usize, fail: Option<&str>) -> Result<(OwnedFd, *mut u8, String), String> {
    let name = name(std::process::id(), NEXT.fetch_add(1, Ordering::Relaxed));
    let c = CString::new(name.clone()).map_err(|_| "shm_open: invalid name".to_string())?;
    let fd = step(fail, "shm_open", || {
        // Exactly these flags: Darwin accepts no others (it makes the
        // descriptor close-on-exec itself; so does glibc).
        let flags = libc::O_RDWR | libc::O_CREAT | libc::O_EXCL;
        #[cfg(target_vendor = "apple")]
        let fd = unsafe { libc::shm_open(c.as_ptr(), flags, 0o600 as libc::c_uint) };
        #[cfg(not(target_vendor = "apple"))]
        let fd = unsafe { libc::shm_open(c.as_ptr(), flags, 0o600) };
        check(fd).map(|fd| unsafe { OwnedFd::from_raw_fd(fd) })
    })?;
    // The name goes at once: the object lives as long as descriptors and
    // mappings do. From here on an error closes `fd`, which frees the object.
    step(fail, "shm_unlink", || check(unsafe { libc::shm_unlink(c.as_ptr()) }))?;
    // The shell must never inherit this descriptor (FD boundary contract).
    step(fail, "fcntl", || {
        let flags = check(unsafe { libc::fcntl(fd.as_raw_fd(), libc::F_GETFD) })?;
        if flags & libc::FD_CLOEXEC == 0 {
            check(unsafe { libc::fcntl(fd.as_raw_fd(), libc::F_SETFD, flags | libc::FD_CLOEXEC) })?;
        }
        Ok(())
    })?;
    // Once: Darwin refuses a second ftruncate of the same object.
    step(fail, "ftruncate", || check(unsafe { libc::ftruncate(fd.as_raw_fd(), bytes as libc::off_t) }))?;
    let map = step(fail, "mmap", || {
        let map = unsafe {
            libc::mmap(
                std::ptr::null_mut(),
                bytes,
                libc::PROT_READ | libc::PROT_WRITE,
                libc::MAP_SHARED,
                fd.as_raw_fd(),
                0,
            )
        };
        if map == libc::MAP_FAILED {
            Err(io::Error::last_os_error())
        } else {
            Ok(map.cast::<u8>())
        }
    })?;
    Ok((fd, map, name))
}

/// The earlier behaviour: a file in the records directory, removed at once.
fn file(dir: &Path, seq: u32, bytes: usize, fail: Option<&str>) -> Result<File, String> {
    let path = dir.join(format!("shm-{seq}"));
    let f = step(fail, "open", || {
        OpenOptions::new().read(true).write(true).create_new(true).mode(0o600).open(&path)
    })?;
    let mode = step(fail, "chmod", || f.set_permissions(std::fs::Permissions::from_mode(0o600)));
    // The name goes whatever the step before said.
    step(fail, "unlink", || std::fs::remove_file(&path))?;
    mode?;
    step(fail, "set_len", || f.set_len(bytes as u64))?;
    Ok(f)
}

impl Backing {
    /// `size` is the buffer in pixels and `bytes` its pool; `seq` numbers the
    /// file of the fallback inside `dir`.
    pub fn new(dir: &Path, seq: u32, size: (usize, usize), bytes: usize, policy: Policy) -> Result<Self, String> {
        let what = format!("shm buffer {}x{} ({bytes} bytes)", size.0, size.1);
        let mut fallback = None;
        if !policy.file_only {
            match posix(bytes, policy.fail_posix()) {
                Ok((fd, map, name)) => {
                    return Ok(Self { store: Store::Posix { fd, map, len: bytes }, what, name: Some(name), fallback: None })
                }
                Err(e) => fallback = Some(format!("posix {e}")),
            }
        }
        match file(dir, seq, bytes, policy.fail_file()) {
            Ok(f) => Ok(Self { store: Store::File(f), what, name: None, fallback }),
            Err(e) => Err(match fallback {
                Some(first) => format!("{what}: file {e} (after {first})"),
                None => format!("{what}: file {e}"),
            }),
        }
    }
    pub fn kind(&self) -> Kind {
        match self.store {
            Store::Posix { .. } => Kind::Posix,
            Store::File(_) => Kind::File,
        }
    }
    /// The descriptor for `wl_shm.create_pool`.
    pub fn fd(&self) -> BorrowedFd<'_> {
        match &self.store {
            Store::Posix { fd, .. } => fd.as_fd(),
            Store::File(f) => f.as_fd(),
        }
    }
    pub fn fallback(&self) -> Option<&str> {
        self.fallback.as_deref()
    }
    #[cfg_attr(not(test), allow(dead_code))]
    pub fn name(&self) -> Option<&str> {
        self.name.as_deref()
    }
    /// "shm buffer WxH (N bytes)".
    pub fn what(&self) -> &str {
        &self.what
    }
    /// Copy `data` to `offset` of the pool.
    pub fn write(&mut self, offset: usize, data: &[u8]) -> Result<(), String> {
        match &mut self.store {
            Store::Posix { map, len, .. } => {
                if offset.checked_add(data.len()).is_none_or(|end| end > *len) {
                    return Err(format!("{}: posix write {} bytes at {offset}: outside the mapping", self.what, data.len()));
                }
                // The mapping is `len` bytes, ours alone to write, and `data`
                // is a separate allocation.
                unsafe { std::ptr::copy_nonoverlapping(data.as_ptr(), map.add(offset), data.len()) };
                Ok(())
            }
            Store::File(f) => f
                .seek(SeekFrom::Start(offset as u64))
                .and_then(|_| f.write_all(data))
                .map_err(|e| format!("{}: file write {} bytes at {offset}: {}", self.what, data.len(), errno(&e))),
        }
    }
}
impl Drop for Backing {
    fn drop(&mut self) {
        if let Store::Posix { map, len, .. } = &self.store {
            // The descriptor closes after this, when the field is dropped.
            unsafe { libc::munmap(map.cast(), *len) };
        }
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    use std::os::unix::fs::DirBuilderExt;

    fn scratch(tag: &str) -> std::path::PathBuf {
        let stamp = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
        let dir = std::env::temp_dir().join(format!("terminal-app-shm-{tag}-{}-{stamp}", std::process::id()));
        std::fs::DirBuilder::new().mode(0o700).create(&dir).unwrap();
        dir
    }
    /// What another process sees through the descriptor: a mapping of its own.
    pub(crate) fn mapped(fd: BorrowedFd<'_>, len: usize) -> Vec<u8> {
        let map = unsafe { libc::mmap(std::ptr::null_mut(), len, libc::PROT_READ, libc::MAP_SHARED, fd.as_raw_fd(), 0) };
        assert_ne!(map, libc::MAP_FAILED, "{}", io::Error::last_os_error());
        let bytes = unsafe { std::slice::from_raw_parts(map.cast::<u8>(), len) }.to_vec();
        assert_eq!(unsafe { libc::munmap(map, len) }, 0);
        bytes
    }
    fn cloexec(fd: BorrowedFd<'_>) -> bool {
        let flags = unsafe { libc::fcntl(fd.as_raw_fd(), libc::F_GETFD) };
        assert!(flags >= 0);
        flags & libc::FD_CLOEXEC != 0
    }
    /// The entry a name has on Linux while it exists.
    fn entry(name: &str) -> std::path::PathBuf {
        Path::new("/dev/shm").join(name.trim_start_matches('/'))
    }
    /// Names of this process still in /dev/shm. Other tests of this process
    /// have one for an instant between `shm_open` and `shm_unlink`, so a
    /// name counts only if it stays.
    pub(crate) fn leftover_names() -> Vec<String> {
        let prefix = format!("cht-{}-", std::process::id());
        let scan = || -> Vec<String> {
            std::fs::read_dir("/dev/shm")
                .unwrap()
                .filter_map(|e| e.ok()?.file_name().into_string().ok())
                .filter(|n| n.starts_with(&prefix))
                .collect()
        };
        let mut left = scan();
        for _ in 0..200 {
            if left.is_empty() {
                break;
            }
            std::thread::sleep(std::time::Duration::from_millis(1));
            left.retain(|n| scan().contains(n));
        }
        left
    }
    fn pattern(len: usize) -> Vec<u8> {
        (0..len).map(|i| (i * 7 + i / 251) as u8).collect()
    }

    #[test]
    fn names_fit_darwin_and_differ() {
        assert_eq!(name(4321, 7), "/cht-4321-7");
        assert!(name(u32::MAX, u32::MAX).len() <= NAME_MAX, "{}", name(u32::MAX, u32::MAX));
        assert_ne!(name(1, 23), name(12, 3));
    }
    #[test]
    fn environment_override_is_only_the_word_file() {
        assert!(Policy::from_env(Some("file")).file_only);
        for other in [None, Some("posix"), Some(""), Some("FILE"), Some("file ")] {
            assert_eq!(Policy::from_env(other), Policy::default(), "{other:?}");
        }
    }
    #[test]
    fn posix_backing_is_unlinked_close_on_exec_zeroed_and_shared() {
        let dir = scratch("posix");
        let (w, h) = (300usize, 70usize);
        let bytes = w * h * 4;
        let mut a = Backing::new(&dir, 1, (w, h), bytes, Policy::default()).unwrap();
        let mut b = Backing::new(&dir, 1, (w, h), bytes, Policy::default()).unwrap();
        assert_eq!((a.kind(), b.kind()), (Kind::Posix, Kind::Posix));
        assert_eq!((a.fallback(), a.what()), (None, "shm buffer 300x70 (84000 bytes)"));
        // Unique per buffer, in Darwin's length, and already gone by name.
        let (name_a, name_b) = (a.name().unwrap().to_string(), b.name().unwrap().to_string());
        assert_ne!(name_a, name_b);
        for name in [&name_a, &name_b] {
            assert!(name.starts_with(&format!("/cht-{}-", std::process::id())) && name.len() <= NAME_MAX, "{name}");
            assert!(!entry(name).exists(), "{name} was unlinked");
            let c = CString::new(name.as_str()).unwrap();
            assert!(unsafe { libc::shm_open(c.as_ptr(), libc::O_RDWR, 0) } < 0, "{name} cannot be opened again");
        }
        assert!(cloexec(a.fd()) && cloexec(b.fd()));
        // The size was set, the memory starts as zeroes, nothing is in the records directory.
        let size = |fd: BorrowedFd<'_>| File::from(fd.try_clone_to_owned().unwrap()).metadata().unwrap().len();
        assert_eq!(size(a.fd()), bytes as u64);
        assert!(mapped(a.fd(), bytes).iter().all(|byte| *byte == 0));
        assert_eq!(std::fs::read_dir(&dir).unwrap().count(), 0);
        // Rows written here are what a second mapping of the descriptor shows.
        let data = pattern(bytes);
        let row = w * 4;
        a.write(0, &data).unwrap();
        b.write(3 * row, &data[3 * row..9 * row]).unwrap();
        assert_eq!(mapped(a.fd(), bytes), data);
        let other = mapped(b.fd(), bytes);
        assert_eq!(other[3 * row..9 * row], data[3 * row..9 * row]);
        assert!(other[..3 * row].iter().chain(&other[9 * row..]).all(|byte| *byte == 0), "the two buffers are separate memory");
        // Outside the pool: refused, and said.
        assert_eq!(a.write(bytes - 2, &[1, 2, 3]).unwrap_err(), "shm buffer 300x70 (84000 bytes): posix write 3 bytes at 83998: outside the mapping");
        // The memory outlives the terminal's side for whoever holds the descriptor.
        let kept = a.fd().try_clone_to_owned().unwrap();
        drop(a);
        drop(b);
        assert_eq!(mapped(kept.as_fd(), bytes), data);
        assert!(!entry(&name_a).exists() && !entry(&name_b).exists());
        assert_eq!(leftover_names(), Vec::<String>::new());
        std::fs::remove_dir_all(dir).unwrap();
    }
    #[test]
    fn file_backing_on_request_is_the_earlier_unlinked_file() {
        let dir = scratch("file");
        let bytes = 64 * 16 * 4;
        let mut f = Backing::new(&dir, 5, (64, 16), bytes, Policy::from_env(Some("file"))).unwrap();
        assert_eq!((f.kind(), f.fallback(), f.name()), (Kind::File, None, None));
        assert!(cloexec(f.fd()));
        assert_eq!(std::fs::read_dir(&dir).unwrap().count(), 0, "the file was unlinked");
        let data = pattern(bytes);
        f.write(256, &data[256..2048]).unwrap();
        let seen = mapped(f.fd(), bytes);
        assert_eq!(seen[256..2048], data[256..2048]);
        assert!(seen[..256].iter().chain(&seen[2048..]).all(|byte| *byte == 0));
        std::fs::remove_dir_all(dir).unwrap();
    }
    #[test]
    fn every_failing_posix_step_cleans_up_and_falls_back_to_the_file() {
        let dir = scratch("fallback");
        let bytes = 40 * 10 * 4;
        for op in ["shm_open", "fcntl", "ftruncate", "mmap"] {
            let policy = Policy { fail_posix: Some(op), ..Policy::default() };
            let mut f = Backing::new(&dir, 1, (40, 10), bytes, policy).unwrap();
            assert_eq!(f.kind(), Kind::File, "{op}");
            assert_eq!(f.fallback(), Some(format!("posix {op}: errno 28").as_str()));
            assert_eq!(leftover_names(), Vec::<String>::new(), "{op}");
            let data = pattern(bytes);
            f.write(0, &data).unwrap();
            assert_eq!(mapped(f.fd(), bytes), data);
            assert_eq!(std::fs::read_dir(&dir).unwrap().count(), 0);
        }
        std::fs::remove_dir_all(dir).unwrap();
    }
    #[test]
    fn allocation_failure_names_buffer_backing_operation_and_errno() {
        let dir = scratch("fail");
        let new = |dir: &Path, policy| Backing::new(dir, 1, (2610, 1632), 2610 * 1632 * 4, policy).err().unwrap();
        // Both backings fail: the file's error first (it ended the attempt), then why the file was tried.
        let both = Policy { fail_posix: Some("ftruncate"), fail_file: Some("set_len"), ..Policy::default() };
        assert_eq!(new(&dir, both), "shm buffer 2610x1632 (17038080 bytes): file set_len: errno 28 (after posix ftruncate: errno 28)");
        assert_eq!(std::fs::read_dir(&dir).unwrap().count(), 0, "the failed file is not left behind");
        // A real errno from the kernel: the records directory does not exist (ENOENT, 2).
        let missing = dir.join("missing");
        assert_eq!(new(&missing, Policy::from_env(Some("file"))), "shm buffer 2610x1632 (17038080 bytes): file open: errno 2");
        let posix_then_missing = Policy { fail_posix: Some("mmap"), ..Policy::default() };
        assert_eq!(new(&missing, posix_then_missing), "shm buffer 2610x1632 (17038080 bytes): file open: errno 2 (after posix mmap: errno 28)");
        // A real write failure: the descriptor only reads (EBADF, 9).
        std::fs::write(dir.join("ro"), b"").unwrap();
        let mut f = Backing {
            store: Store::File(File::open(dir.join("ro")).unwrap()),
            what: "shm buffer 8x2 (64 bytes)".into(),
            name: None,
            fallback: None,
        };
        assert_eq!(f.write(32, &[0; 32]).unwrap_err(), "shm buffer 8x2 (64 bytes): file write 32 bytes at 32: errno 9");
        std::fs::remove_dir_all(dir).unwrap();
    }
}
