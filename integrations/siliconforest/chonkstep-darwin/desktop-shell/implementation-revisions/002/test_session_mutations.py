#!/usr/bin/env python3
"""Host-only mutations of the actual shared session coordinator and its tests."""
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
SOURCE = HERE / "source/crates/wm-darwin/src/session_entry.rs"


def run(source, directory, name):
    path = directory / (name + ".rs")
    binary = directory / name
    path.write_text(source)
    compile = subprocess.run(["rustc", "--edition=2021", "--test", str(path), "-o", str(binary)], capture_output=True, text=True, check=False)
    if compile.returncode:
        raise RuntimeError(compile.stdout + compile.stderr)
    return subprocess.run([str(binary)], capture_output=True, text=True, check=False)


def main():
    original = SOURCE.read_text()
    changes = {
        "skip_shutdown": ("    io.begin_shutdown();", "    // injected omission"),
        "skip_cleanup": ("result.cleanup = io.cleanup();", "result.cleanup = CleanupState::Complete;"),
        "timeout_claims_complete": ("            result.cleanup = CleanupState::Incomplete;", "            result.cleanup = CleanupState::Complete;"),
        "discard_frame_error": ("                result.frame_error = Some(error);", "                let _ = error;"),
        "discard_seal_error": ("        result.seal_error = Some(error);", "        let _ = error;"),
        "bypass_ui_unwind_boundary": ("match catch_unwind(AssertUnwindSafe(|| io.frame())) {", "match Ok::<_, Box<dyn std::any::Any + Send>>(io.frame()) {"),
    }
    with tempfile.TemporaryDirectory(prefix="chonk-session-mutations-") as temp:
        directory = Path(temp)
        baseline = run(original, directory, "baseline")
        if baseline.returncode:
            raise RuntimeError(baseline.stdout + baseline.stderr)
        for name, (old, new) in changes.items():
            if original.count(old) != 1:
                raise RuntimeError(f"mutation anchor changed: {name}")
            result = run(original.replace(old, new), directory, name)
            if result.returncode == 0 or "test result: FAILED" not in result.stdout:
                raise RuntimeError(f"mutation not assertion-killed: {name}\n{result.stdout}\n{result.stderr}")
            print(f"assertion-killed {name}")
    print("Baseline4 tests passed; 6 actual source coordinator mutations rejected; host only")


if __name__ == "__main__":
    main()
