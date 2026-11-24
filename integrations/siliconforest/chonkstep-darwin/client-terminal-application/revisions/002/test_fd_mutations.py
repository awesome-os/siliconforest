#!/usr/bin/env python3
"""Compile/run real host PTY source mutants; no target or Darwin proof."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent


def main():
    if __import__("sys").flags.optimize:
        raise RuntimeError("mutation checks require assertions")
    original = (HERE / "src/pty.rs").read_text()
    controls = [
        ("skip-close", "            libc::close(fd);", "            /* deliberately skip close */", "pty::tests::host_real_pre_exec_fd_check_and_exec_without_proc", "stage=7"),
        ("skip-verification", "    verify_child_fds(pipe, maxfd);", "    /* deliberately skip verification */", "pty::tests::host_live_extra_fd_cannot_become_checked_setup_or_exec", "called `Result::unwrap_err()` on an `Ok` value"),
    ]
    validation = HERE / "validation"
    validation.mkdir(exist_ok=True)
    results = []
    for name, before, after, test, diagnostic in controls:
        assert original.count(before) == 1, (name, "unique mutation required")
        with tempfile.TemporaryDirectory(prefix="terminal-fd-mutant-") as temporary:
            work = Path(temporary)
            for file in ("Cargo.toml", "Cargo.lock"):
                shutil.copy2(HERE / file, work / file)
            shutil.copytree(HERE / "src", work / "src")
            shutil.copytree(HERE / "fonts", work / "fonts")
            (work / "src/pty.rs").write_text(original.replace(before, after, 1))
            # Never share package artifacts across temporary source roots or
            # with the original crate: a cached mutant must not become the
            # executable used by a later original-source test invocation.
            command = ["cargo", "test", "--manifest-path", str(work / "Cargo.toml"), "--locked", "--offline", "--target-dir", str(work / "target"), test, "--", "--exact", "--test-threads=1"]
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=90)
            log = result.stdout
            path = validation / f"mutant-{name}.log"
            path.write_text("Host-only source mutant; not target execution.\n$ " + " ".join(command) + "\n" + log)
            # Reject a test failure only after real compilation and the actual
            # selected test ran; a compiler error is not mutation coverage.
            compiled = "Compiling chonk-terminal-darwin" in log and "Finished `test` profile" in log and "error[E" not in log
            ran = f"test {test} ... FAILED" in log and "running 1 test" in log
            rejected = result.returncode != 0 and compiled and ran and diagnostic in log
            results.append({"name": name, "test": test, "exit_code": result.returncode, "compiled": compiled, "selected_test_ran": ran, "rejected": rejected, "log": str(path.relative_to(HERE)), "log_sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "mutant_pty_sha256": hashlib.sha256((work / "src/pty.rs").read_bytes()).hexdigest()})
            assert rejected, (name, log)
    assert (HERE / "src/pty.rs").read_text() == original
    (validation / "fd-mutations.json").write_text(json.dumps({"scope": "Linux host PTY/fcntl/exec source mutations only; no Darwin execution or DS4 admission", "controls": results}, indent=2) + "\n")
    print(json.dumps({"compiling_host_mutants_rejected": len(results), "source_unchanged": True}))


if __name__ == "__main__":
    main()
