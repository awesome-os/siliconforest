#!/usr/bin/env python3
"""Exact cumulative/incremental source replay; no build or device operations."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
LAB = HERE.parents[5]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_files(root):
    # The original fixture tree contains an internal directory alias; the001
    # working copy materialized it. Compare logical bytes, not symlink storage.
    result = {}
    for name, directories, files in os.walk(root, followlinks=True):
        directory = Path(name)
        assert directory.resolve().is_relative_to(root.resolve()), directory
        directories[:] = [d for d in directories if d not in {"target", ".git", "__pycache__"}]
        for name in files:
            path = directory / name
            assert path.resolve().is_relative_to(root.resolve()), path
            result[path.relative_to(root).as_posix()] = sha(path)
    return result


def set_sha(files):
    return hashlib.sha256("".join(f"{value}  {key}\n" for key, value in sorted(files.items())).encode()).hexdigest()


def replay(patch, baseline, changes):
    with tempfile.TemporaryDirectory(prefix="darwin-shell006-replay-") as name:
        scratch = Path(name)
        for path, hashes in changes.items():
            if hashes["before"] is not None:
                original = baseline / path
                assert sha(original) == hashes["before"], path
                output = scratch / path
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_bytes(original.read_bytes())
        result = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(HERE / patch)], cwd=scratch, capture_output=True, text=True, check=False)
        assert result.returncode == 0, result.stdout + result.stderr
        for path, hashes in changes.items():
            assert sha(scratch / path) == hashes["after"], path
            assert sha(HERE / "source" / path) == hashes["after"], path


def main():
    if sys.flags.optimize:
        raise RuntimeError("source verification requires assertions enabled")
    receipt = json.loads((HERE / "source-validation.json").read_text())
    for path, digest in receipt["files_sha256"].items():
        assert sha(HERE / path) == digest, path
    for path, digest in receipt["dependency_files_sha256"].items():
        assert sha(LAB / path) == digest, path
    original = LAB / receipt["baseline"]["path"]
    checkpoint = HERE.parent / "005" / "source"
    replay("shell.patch", original, receipt["changed_files"])
    replay("from-005.patch", checkpoint, receipt["incremental_changed_files"])
    assert set_sha(source_files(HERE / "source")) == receipt["full_source_tree_sha256"]
    assert set_sha({k: v["after"] for k, v in receipt["changed_files"].items()}) == receipt["source_set_sha256"]
    print(f"Verified006 cumulative {len(receipt['changed_files'])} and incremental {len(receipt['incremental_changed_files'])} files, full tree and dependencies; source only")


if __name__ == "__main__":
    main()
