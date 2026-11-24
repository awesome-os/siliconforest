#!/usr/bin/env python3
"""Host-only actual Rust policy tests/mutations and native-entry wiring check.

Run after the offline Cargo host suite supplies its exact libc rlib. The tests
construct local sigaction values and fake dispositions; they never replace the
Python/test-runner process signal handlers or open a device.
"""
import hashlib
import re
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
SOURCE = HERE / "source/crates/wm-darwin/src/session_signals.rs"


def rust_test(text, name, scratch, library):
    path = scratch / (name + ".rs")
    path.write_text(text)
    binary = scratch / name
    result = subprocess.run([
        "rustc", "--edition=2021", "--test", str(path),
        "--extern", "libc=" + str(library), "-L", "dependency=" + str(library.parent),
        "-o", str(binary),
    ], text=True, capture_output=True, check=False)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return subprocess.run([str(binary)], text=True, capture_output=True, check=False, timeout=15)


def main():
    source = SOURCE.read_text()
    libraries = list((HERE / "source/target/debug/deps").glob("liblibc-*.rlib"))
    if len(libraries) != 1:
        raise RuntimeError("Expected one exact libc rlib from003 offline Cargo tests")
    library = libraries[0]
    entry = (SOURCE.parent / "desktop_entry.rs").read_text()
    run = entry.split("pub fn run(options: Options)", 1)[1].split("fn run_session", 1)[0]
    if "with_process_signals" not in run or "|| run_session(options)" not in run:
        raise RuntimeError("native entry lost its checked signal wrapper")
    if any(x in run for x in ("NativeHid::open", "Pdgop::open", "ListeningSocket::bind", "TerminalLauncher::new")):
        raise RuntimeError("device/process admission moved outside the signal scope")
    if "libc::signal(" in entry or "STOP.store" in entry.split("pub fn run(options: Options)", 1)[1]:
        raise RuntimeError("unchecked handler replacement or stop reset bypasses the scope")
    if source.index("let _claim = EntryClaim::new") > source.index("stop_flag.store"):
        raise RuntimeError("stop reset precedes exclusive claim")
    changes = {
        "pipe_default": ("(libc::SIGPIPE, libc::SIG_IGN),", "(libc::SIGPIPE, libc::SIG_DFL),"),
        "child_ignored": ("(libc::SIGCHLD, libc::SIG_DFL),", "(libc::SIGCHLD, libc::SIG_IGN),"),
        "autoreap_flag": ("action.sa_flags = 0;", "action.sa_flags = libc::SA_NOCLDWAIT;"),
        "skip_restore": ("ops.restore(signal, &action)", "Ok::<(), i32>(())"),
        "restore_error_success": ("Ok(Ok(value)) if restore.is_empty() => Ok(value),", "Ok(Ok(value)) => Ok(value),"),
        "skip_setup_rollback": ("restore: restore_all(ops, &mut saved),", "restore: Vec::new(),"),
        "ignore_poisoned_state": ("if poisoned.load(Ordering::Acquire) {", "if false {"),
    }
    with tempfile.TemporaryDirectory(prefix="darwin-signal-policy-") as temp:
        scratch = Path(temp)
        baseline = rust_test(source, "baseline", scratch, library)
        if baseline.returncode:
            raise RuntimeError(baseline.stdout + baseline.stderr)
        match = re.search(r"test result: ok\. (\d+) passed", baseline.stdout)
        if not match:
            raise RuntimeError("missing baseline test result")
        for name, (before, after) in changes.items():
            # Only production prefix is mutated, never the assertion oracle.
            production, tests = source.split("#[cfg(test)]", 1)
            if production.count(before) != 1:
                raise RuntimeError("mutation anchor changed: " + name)
            mutant = production.replace(before, after) + "#[cfg(test)]" + tests
            result = rust_test(mutant, name, scratch, library)
            if result.returncode == 0 or "test result: FAILED" not in result.stdout:
                raise RuntimeError("not assertion-killed: " + name + result.stdout + result.stderr)
            print("assertion-killed", name)
    print("libc_rlib_sha256", hashlib.sha256(library.read_bytes()).hexdigest())
    print(f"Actual policy baseline {match.group(1)} tests,7 assertion-killed mutants,entry scope PASS; host only")


if __name__ == "__main__":
    main()
