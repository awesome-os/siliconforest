#!/usr/bin/env python3
"""Exact-pin, offline staging of the bounded T8103 first-entry experiment.

Dry run by default. This never fetches, builds, boots, commits or edits the lab's
baseline/ram-probe checkouts. Use only a separate, clean local checkout.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
LAB = HERE.parents[2]
PIN = "e7aa00a60f0b56ef35aa24252e81467787bf71ea"


class StageError(ValueError):
    pass


def git(root: Path, *args: str, data: bytes | None = None) -> bytes:
    result = subprocess.run(
        ["git", "-c", "core.hooksPath=/dev/null", "-C", str(root), *args],
        input=data, capture_output=True, check=False, timeout=60)
    if result.returncode:
        raise StageError(result.stderr.decode(errors="replace").strip())
    return result.stdout


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def regular(path: Path) -> None:
    if any(p.is_symlink() for p in (path, *path.parents)):
        raise StageError("Symlink path refused: " + str(path))
    if not path.is_file():
        raise StageError("Missing regular file: " + str(path))


def prepare(checkout: Path) -> tuple[Path, bytes, dict]:
    if any(p.is_symlink() for p in (checkout, *checkout.parents)):
        raise StageError("Checkout symlink refused")
    root = checkout.resolve(strict=True)
    protected = [LAB / "sources" / name for name in
                 ("puredarwin-baseline", "puredarwin-ram-probe")]
    if any(root == p or root.is_relative_to(p) for p in protected):
        raise StageError("Baseline and RAM-probe checkouts are read-only to this stager")
    if Path(git(root, "rev-parse", "--show-toplevel").decode().strip()).resolve() != root:
        raise StageError("Use the checkout root")
    if git(root, "rev-parse", "HEAD").decode().strip() != PIN:
        raise StageError("Wrong commit; exact pin required: " + PIN)
    # ls-files is also checked below: assume-unchanged must not hide target drift.
    if git(root, "status", "--porcelain", "--untracked-files=all").strip():
        raise StageError("Checkout must be clean, including untracked files")
    regular(HERE / "candidate.patch")
    regular(HERE / "manifest.json")
    patch = (HERE / "candidate.patch").read_bytes()
    manifest = json.loads((HERE / "manifest.json").read_text())
    if manifest["pin"] != PIN or sha256(patch) != manifest["patch_sha256"]:
        raise StageError("Patch manifest mismatch")
    for name, record in manifest["files"].items():
        relative = Path(name)
        if relative.is_absolute() or ".." in relative.parts:
            raise StageError("Unsafe patch path")
        path = root / relative
        if any(p.is_symlink() for p in (path, *path.parents)):
            raise StageError("Linked destination refused: " + name)
        if record["before_sha256"] is None:
            if path.exists():
                raise StageError("Addition already exists: " + name)
        else:
            regular(path)
            expected = git(root, "show", PIN + ":" + name)
            if (sha256(expected) != record["before_sha256"] or
                    path.read_bytes() != expected):
                raise StageError("Pinned source bytes differ: " + name)
    git(root, "apply", "--check", "--whitespace=error-all", "-", data=patch)
    return root, patch, manifest


def stage(checkout: Path, execute: bool = False) -> dict:
    root, patch, manifest = prepare(checkout)
    if execute:
        # Mutation only after all files and the whole patch have passed preflight.
        git(root, "apply", "--whitespace=error-all", "-", data=patch)
        for name, record in manifest["files"].items():
            if sha256((root / name).read_bytes()) != record["after_sha256"]:
                raise StageError("Post-stage bytes differ: " + name)
    return {
        "status": "STAGED_UNBUILT_SOURCE" if execute else "READY_TO_STAGE_UNBUILT_SOURCE",
        "checkout": str(root), "pin": PIN,
        "patch_sha256": manifest["patch_sha256"], "files": manifest["files"],
        "executed": execute, "kernel_built": False, "runtime_proof": False,
        "full_kernel_init_supported": False, "mapped_reclamation_enabled": False,
        "build_attribute": "kernel-arm64-t8103-firstboot",
        "contract": str(HERE / "CONTRACT.md"),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checkout", type=Path, required=True)
    parser.add_argument("--execute", action="store_true")
    args = parser.parse_args()
    try:
        print(json.dumps(stage(args.checkout, args.execute), indent=2))
        return 0
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(json.dumps({"status": "BLOCKED", "error": str(error)}), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
