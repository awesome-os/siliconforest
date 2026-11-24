#!/usr/bin/env python3
"""Verify and replay this source-only patch in a temporary host directory."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
LAB = HERE.parents[3]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    if sys.flags.optimize:
        raise RuntimeError("source verifier requires assertions enabled")
    receipt = json.loads((HERE / "source-validation.json").read_text())
    baseline = LAB / receipt["baseline"]["path"]
    for path, expected in receipt["files_sha256"].items():
        assert digest(HERE / path) == expected, path
    for path, expected in receipt["dependency_files_sha256"].items():
        assert digest(LAB / path) == expected, path
    with tempfile.TemporaryDirectory(prefix="darwin-shell-replay-") as temporary:
        work = Path(temporary)
        for path, hashes in receipt["changed_files"].items():
            target = work / path
            target.parent.mkdir(parents=True, exist_ok=True)
            if hashes["before"] is not None:
                assert digest(baseline / path) == hashes["before"], path
                target.write_bytes((baseline / path).read_bytes())
        result = subprocess.run(
            ["patch", "--batch", "--fuzz=0", "-p1", "-i", str(HERE / "shell.patch")],
            cwd=work, text=True, capture_output=True, check=False,
        )
        assert result.returncode == 0, result.stdout + result.stderr
        for path, hashes in receipt["changed_files"].items():
            assert digest(work / path) == hashes["after"], path
            if (HERE / "source" / path).exists():
                assert digest(HERE / "source" / path) == hashes["after"], path
    print(f"Verified exact source patch replay: {len(receipt['changed_files'])} files; no native build or execution")


if __name__ == "__main__":
    main()
