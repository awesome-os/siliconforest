#!/usr/bin/env python3
"""Host-only checks for the sfhid-keyboard compositor patch.

Applies the patch to a temporary copy of the frozen wm-darwin files, compiles
hid.rs with the already-cached libc crate, and checks the seat wiring in the
patched sources. Does not open /dev/usb_hid_kbd or /dev/usb_hid_mouse, does not
modify the frozen tree, and does not claim native input.
"""

from __future__ import annotations

import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
LAB = ROOT.parents[3]
FROZEN = LAB / "sources" / "chonkstep-darwin"
PATCH = ROOT / "sfhid-keyboard.patch"
RELATIVE = (
    "crates/wm-darwin/src/hid.rs",
    "crates/wm-darwin/src/input.rs",
    "crates/wm-darwin/src/main.rs",
    "crates/wm-darwin/README.md",
)
BEFORE = {
    "crates/wm-darwin/src/hid.rs": "eace58887c692cef0f160960eb52c9888f71e50d9e909f609b8c344a8a024bd9",
    "crates/wm-darwin/src/input.rs": "26bbb8637e0f8902069128f144347c3d6ca8aa4fea5dde3f712053abc0cb0431",
    "crates/wm-darwin/src/main.rs": "99ffbeaa82375db72bfc27f2a64694664216ceec76d0aa5955004692b3e9977e",
    "crates/wm-darwin/README.md": "7ac2b7e623ff4600e2d0afc0cc7a234ba93ca9c11ca2a8b95640b7f41730d8c7",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def check_frozen() -> None:
    for rel, expected in BEFORE.items():
        path = FROZEN / rel
        require(path.is_file(), f"missing frozen file {path}")
        got = sha256(path)
        require(got == expected, f"frozen hash changed for {rel}: {got}")


def apply_patch(tree: Path) -> None:
    for rel in RELATIVE:
        destination = tree / rel
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(FROZEN / rel, destination)
    subprocess.run(
        ["patch", "-p1", "--forward", "--batch"],
        input=PATCH.read_bytes(),
        cwd=tree,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def check_wiring(tree: Path) -> None:
    hid = (tree / "crates/wm-darwin/src/hid.rs").read_text()
    input_rs = (tree / "crates/wm-darwin/src/input.rs").read_text()
    main_rs = (tree / "crates/wm-darwin/src/main.rs").read_text()
    readme = (tree / "crates/wm-darwin/README.md").read_text()
    require('"/dev/usb_hid_kbd"' in hid and '"/dev/usb_hid_mouse"' in hid, "device paths missing")
    require("InputMode::KeyboardOnly => Ok((open_keyboard()?, None))" in hid, "keyboard-only still opens a mouse")
    require("fn acquire(mode: InputMode)" in hid, "acquire helper missing")
    require('target_os = "macos", target_arch = "aarch64"' in hid, "native open is not target-gated")
    require("O_NONBLOCK | libc::O_NOFOLLOW | libc::O_CLOEXEC" in hid, "open flags changed")
    require('if caps.pointer{Some(seat.add_pointer())}else{None}' in input_rs, "pointer seat is unconditional")
    require("accept_for_seat(caps,&event)?" in input_rs, "deliver_hid does not use the seat gate")
    require("SeatCaps{keyboard:true,pointer:true}" in input_rs, "existing sfhid seat lost pointer")
    require("NativeHid::open_keyboard()" in main_rs, "main does not select keyboard-only open")
    require("InputMode::Off => None" in main_rs, "default input-off path missing")
    require("NativeHid::open()" in main_rs, "existing both-device open missing")
    require("sfhid-keyboard" in readme and "not\nnative input" in readme, "README contract missing")
    require("usb_hid" not in main_rs, "main grew a direct device path")


def cargo_test(hid_rs: Path, project: Path) -> None:
    project.mkdir()
    manifest = project / "Cargo.toml"
    manifest.write_text(
        """[package]
name = "sfhid_keyboard_host"
version = "0.0.0"
edition = "2021"
publish = false

[dependencies]
libc = "=0.2.189"
""",
        encoding="utf-8",
    )
    source = project / "src"
    source.mkdir()
    shutil.copy2(hid_rs, source / "hid.rs")
    (source / "lib.rs").write_text(
        "#[allow(dead_code)]\n#[path = \"hid.rs\"]\nmod hid;\n",
        encoding="utf-8",
    )
    target = project / "target"
    env = os.environ.copy()
    env["CARGO_TARGET_DIR"] = str(target)
    completed = subprocess.run(
        ["cargo", "test", "--offline", "--manifest-path", str(manifest), "--", "--test-threads=1"],
        cwd=project,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    sys.stdout.write(completed.stdout)
    require(completed.returncode == 0, "host hid.rs tests failed")
    require("native input confirmed" not in completed.stdout.lower(), "test output claimed native input")


def main() -> int:
    require(PATCH.is_file(), f"missing {PATCH}")
    check_frozen()
    with tempfile.TemporaryDirectory(prefix="sfhid-keyboard-") as tmp:
        tree = Path(tmp) / "tree"
        project = Path(tmp) / "host"
        apply_patch(tree)
        check_wiring(tree)
        cargo_test(tree / "crates/wm-darwin/src/hid.rs", project)
        after = {rel: sha256(tree / rel) for rel in RELATIVE}
    check_frozen()
    print("HOST_ONLY")
    print("native_input_claimed false")
    print("darwin_pass 0")
    print("failures 0")
    for rel, digest in after.items():
        print(f"after {rel} {digest}")
    print(f"patch {sha256(PATCH)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
