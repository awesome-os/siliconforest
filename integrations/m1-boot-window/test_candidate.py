#!/usr/bin/env python3
"""Offline tests: production pure C policy plus dispatcher/source selection checks."""
from pathlib import Path
import os
import hashlib
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "sources/m1n1-boot-window"
PIN = "121fcbf35d4501814b58ca5347926a4173b6a512"


def main():
    assert subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=SOURCE, text=True).strip() == PIN
    for flags, expected in [
        (["M1_BOOT_WINDOW=1", "ESP_STAGE2=0"], "requires ESP_STAGE2=1"),
        (["M1_BOOT_WINDOW=1", "ESP_STAGE2=1", "CHAINLOADING=0"], "requires CHAINLOADING=1"),
    ]:
        result = subprocess.run(["make", "-n", *flags], cwd=SOURCE, capture_output=True, text=True)
        assert result.returncode != 0 and expected in result.stderr
    manifest = json.loads(Path(__file__).with_name("manifest.json").read_text())
    patch = Path(__file__).with_name("candidate.patch")
    assert hashlib.sha256(patch.read_bytes()).hexdigest() == manifest["patch_sha256"]
    # Reconstruct only the six owned files in a temporary directory, never the baseline checkout.
    with tempfile.TemporaryDirectory(prefix="darwin-window-patch-") as directory:
        staged = Path(directory)
        for name, entry in manifest["files"].items():
            assert hashlib.sha256((SOURCE / name).read_bytes()).hexdigest() == entry["candidate_sha256"]
            if entry["baseline_sha256"] is not None:
                baseline = subprocess.check_output(["git", "show", f"{PIN}:{name}"], cwd=SOURCE)
                assert hashlib.sha256(baseline).hexdigest() == entry["baseline_sha256"]
                (staged / name).parent.mkdir(parents=True, exist_ok=True)
                (staged / name).write_bytes(baseline)
        subprocess.run(["git", "apply", "--check", str(patch)], cwd=staged, check=True)
        subprocess.run(["git", "apply", str(patch)], cwd=staged, check=True)
        for name, entry in manifest["files"].items():
            assert hashlib.sha256((staged / name).read_bytes()).hexdigest() == entry["candidate_sha256"]
    with tempfile.TemporaryDirectory(prefix="darwin-window-test-") as directory:
        binary = str(Path(directory) / "test_window")
        subprocess.run([
            "clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-I", str(SOURCE / "src"), str(SOURCE / "src/m1_boot_window.c"),
            str(Path(__file__).with_name("test_window.c")), "-o", binary,
        ], check=True)
        # Policy uses no allocation; LSan cannot inspect processes under this tool's ptrace.
        subprocess.run([binary], check=True, env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
    original = subprocess.check_output(["git", "show", f"{PIN}:src/uartproxy.c"], cwd=SOURCE, text=True)
    current = (SOURCE / "src/uartproxy.c").read_text()
    # Actual normal feature negotiation and response serialization remain byte-identical.
    for begin, end in [("        switch (request.type) {", "    return ret;")]:
        assert original.split(begin, 1)[1].split(end, 1)[0] == current.split(begin, 1)[1].split(end, 1)[0]
    assert "memcpy(&window_request.type, window.frame[port], REQ_SIZE)" in current
    assert "request = window_request;\n            use_window_request = false;" in current
    assert "if (!start && !use_window_request)" in current
    window = current.split("bool uartproxy_wait_m1_boot_window", 1)[1].split("#endif", 1)[0]
    assert "iodev_read(id, &byte, 1)" in window
    assert "iodev_can_read(id) <= 0" in window
    assert "IODEV_DOCKCHANNEL" not in window and "iodev_write" not in window
    main_source = (SOURCE / "src/main.c").read_text()
    branch = main_source.split("#ifdef M1_BOOT_WINDOW", 1)[1].split("#endif", 1)[0]
    assert branch.index("usb_up = true") < branch.index("usb_iodev_init()")
    assert branch.count("usb_iodev_init()") == 1
    assert "while (!next_stage.entry)" in branch
    assert branch.index("return;") < branch.index("window_timed_out = true;")
    native = main_source.split("if (payload_action == STAGE1_PAYLOAD_NATIVE) {", 1)[1].split('printf("Valid payload found', 1)[0]
    assert "if (window_timed_out)\n            usb_hpm_restore_irqs(true);" in native
    assert main_source.index("payload_run()") < main_source.index("usb_hpm_restore_irqs(true)")
    assert main_source.count("usb_hpm_restore_irqs(true)") == 1
    usb = (SOURCE / "src/usb.c").read_text()
    match = usb.split("static bool usb_hpm_restore_irqs_match", 1)[1].split("static int usb_hpm_restore_irqs_one", 1)[0]
    assert "if (!tps6598x_irq_state[idx].valid)\n        return false;" in match
    assert "idx < FIRST_USB_IODEV || idx >= USB_IODEV_COUNT" in match
    tps = (SOURCE / "src/tps6598x.c").read_text().split("int tps6598x_disable_irqs", 1)[1].split("int tps6598x_restore_irqs", 1)[0]
    assert tps.index("if (read != CD3218B12_IRQ_WIDTH)") < tps.index("state->valid = 1;") < tps.index("TPS_REG_INT_CLEAR1")
    assert main_source.index("uartproxy_wait_m1_boot_window") < main_source.index("payload_run()")
    print("PASS: patch round-trip, manifest hashes, normal dispatcher/replies, bounded pre-payload selection, W1 native IRQ restoration/partial-init guards")


if __name__ == "__main__":
    main()
