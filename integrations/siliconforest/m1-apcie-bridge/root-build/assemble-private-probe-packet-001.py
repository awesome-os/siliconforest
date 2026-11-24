#!/usr/bin/env python3
"""Assemble local artifacts only; no deployment, actor or target access."""
from datetime import datetime, timezone
import difflib
import hashlib
import json
import os
from pathlib import Path
import shutil

LAB = Path(__file__).resolve().parents[4]
BRIDGE = LAB / "integrations/puredarwin/m1-apcie-bridge"
OUT = LAB / "artifacts/m1-desktop-010/native-private-probe-candidate-001"
RAM = LAB / "artifacts/m1-desktop-010/native-private-probe-ramdisk-001/ramdisk.img"
KC = Path("/nix/store/iaxwji3m4k615ds4j52dzskkgw1ar5zf-puredarwin-kc-arm64-0.1/kernel")
HOST = BRIDGE / "native_policy_host_revision/001"
FROZEN = {
    "private_lifecycle/source-validation-001.json": "c585fd8b80479975a46ad19c83b7610a53f3a4a1a420241cbe0022a2d1fb82fb",
    "native_owned/source-validation-001.json": "0ee2101c9cda5cf282c3db48b7640cef98146c3b04724e3fe59138aaad772434",
    "owned_control/source-validation-001.json": "9844eb28efb1190097369b419b90aeeebb605f4aafa147dbd908a7841b3d2c57",
    "mcc_snapshot/source-validation-001.json": "5060b3baf529c046c03429ca7da882f4f1671e8864cd69aba62714148cec26d8",
    "native_policy_snapshot/source-validation-001.json": "908e29b3bd8910018810eda881ec7b4a98a67df354f4a4a3ee2395b705c09302",
    "native_policy_reader/source-validation-001.json": "e892e8e31f0ba8626d4e8a83188b6fe9462163394b1685e5b1094486fcaea93c",
    "native_private_policy/source-validation-001.json": "7d41f394d77e20bd5776964c5a46194b120a3df705ccb996889cb6d852a09c72",
    "native_private_probe/source-validation-001.json": "71e7e8456216ef85ece55419637096af5f4be08dcc13323d87d86a98041bdb70",
    "adt_source_inventory/source-validation-001.json": "c55beefefc40e0a787103d6411ec54f1bccce7f943b2f31ff97deb62936f9260",
    "native_policy_producer/source-validation-001.json": "5e86957056adae64b64c45d15047fbcd3af23198b0de9397141b0bfaec5f2e14",
    "native_policy_handoff_002/source-validation-002.json": "9c67dbb1cbb06a66195744c8296a61bccbdf7cafa2ff6408af8486f928bb23be",
    "mcc_capture/source-validation-001.json": "d2114baa4439e80f8a0f364faedac5b3353236add54cf6615b54e58bf32fd258",
    "native_bootargs/source-validation-001.json": "67b0364799650eff3ed37922cafab77cba0e4d7f5faa52838881290c4fa5ed7b",
    "native_bootargs_abi/source-validation-002.json": "baab08e27e73e3451af40aaba3c24daecebff15726b4014114c595e5d5c82d08",
}
REPORTS = (
    "bcm4378-native-private-probe-source-admission-001.json",
    "bcm4378-native-private-policy-nix-objects-admission-001.json",
    "bcm4378-native-private-probe-sdk-bundle-admission-001.json",
    "bcm4378-native-private-probe-kc-build-and-binding-001.json",
    "bcm4378-native-private-probe-kc-retained-independent-review-001.json",
    "bcm4378-native-private-probe-kc-member-independent-review-001.json",
    "bcm4378-native-private-probe-dyld-binder-source-facts-001.json",
    "bcm4378-private-probe-ramdisk-independent-review-001.json",
    "bcm4378-native-policy-host-source-admission-001.json",
    "bcm4378-private-probe-qualification-contract-001.json",
    "bcm4378-private-probe-allocation-scope-clarification-001.json",
    "bcm4378-private-collector-launcher-source-001.json",
    "bcm4378-private-collector-launcher-independent-review-001.json",
)


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def require(ok, message):
    if not ok:
        raise ValueError(message)


def evidence(path):
    return {"path": str(path.relative_to(LAB)), "sha256": sha(path), "bytes": path.stat().st_size}


def main():
    require(sha(KC) == "2a2b9c125faa5b376d1c2f46bfd220d340443c3d2e70c15af40b0cf1f61a3c21", "KC changed")
    require(sha(RAM) == "129152ba4691b8b4ba1748cef74acdc8936484234e93cc6df45cfde302920669", "RAM changed")
    require(sha(HOST / "source-validation-001.json") == "ad3242cf2d36f0dd9686b1aa030535269ddc74b58f08cd44c618a29186199524", "host proposal changed")
    sources, receipts = {}, []
    for name, expected in sorted(FROZEN.items()):
        path = BRIDGE / name
        require(sha(path) == expected, "receipt changed: " + name)
        receipt = json.loads(path.read_text())
        receipts.append(evidence(path))
        for relative, digest in receipt["files_sha256"].items():
            source = path.parent / relative
            require(sha(source) == digest, "frozen source changed: " + str(source))
            sources[str(source.relative_to(LAB))] = source
    for name in ("compile-private-policy-001.py", "build-private-policy-objects-001.nix",
                 "CMakeLists-private-probe-001.txt", "build-private-probe-bundle-001.nix",
                 "build-private-probe-kc-001.nix", "darwin-native-private-test",
                 "test_private_probe_launcher.py", "stage-private-ramdisk-001.py",
                 "assemble-private-probe-packet-001.py"):
        path = BRIDGE / "root-build" / name
        sources[str(path.relative_to(LAB))] = path
    host_receipt = json.loads((HOST / "source-validation-001.json").read_text())
    host_files = {}
    for name, values in host_receipt["changed_runtime_files"].items():
        path = HOST / "after" / name
        require(sha(path) == values["after"], "host runtime source changed")
        sources[str(path.relative_to(LAB))] = path
        host_files[name] = values["after"]
    baseline = LAB / "artifacts/m1-desktop-010/trackpad-input-trace-candidate-001/source.patch"
    require(sha(baseline) == "407e3310fa17c335176637f03a821ec86ff67c092d3d3ada318fa3f7ef6afab0", "retained source envelope changed")
    patch = baseline.read_text()
    patch += "\n# COMPONENT Original private lifecycle sources and reviewed build/host glue\n"
    patch += "# These are separately composed components, not a single upstream checkout replay.\n"
    for name, source in sorted(sources.items()):
        patch += "\n# SOURCE " + name + " sha256=" + sha(source) + "\n"
        patch += "".join(difflib.unified_diff([], source.read_text().splitlines(keepends=True),
                                            fromfile="/dev/null", tofile="b/" + name))
    old = json.loads((LAB / "artifacts/m1-desktop-010/apcie-port-candidate-003/build-record.json").read_text())
    protection = old["components"]["native_kernel"]["development_protection_configuration"]
    require(protection["kernel_text_hardware_locked"] is False and protection["PPL_protection_claimed"] is False,
            "development protection receipt changed")
    ram_record = RAM.with_suffix(".record.json")
    record = {
        "status": "BUILT_NOT_BOOTED", "build_exit_code": 0,
        "recorded_at": datetime.now(timezone.utc).isoformat(),
        "source_commit": "e7aa00a60f0b56ef35aa24252e81467787bf71ea",
        "corecrypto_commit": "ea8e6afec1a8666a077c110fc4148fb1ac293ae8",
        "kernel_sha256": sha(KC), "source_patch_sha256": hashlib.sha256(patch.encode()).hexdigest(),
        "drv_path": "/nix/store/al9jzaaiag4vw7bgl9nq5glchmbv46ca-puredarwin-kc-arm64-0.1.drv",
        "output_path": str(KC.parent),
        "components": {
            "native_kernel": {"development_protection_configuration": protection},
            "frozen_source_receipts": receipts,
            "source_files_sha256": {name: sha(path) for name, path in sorted(sources.items())},
            "reviewed_components": [evidence(LAB / "reports/sprint-010" / n) for n in REPORTS],
            "retained_native50_source_envelope": evidence(baseline),
            "ram_payload_record": evidence(ram_record),
            "isolated_host_proposal": evidence(HOST / "source-validation-001.json"),
        },
        "native_bootargs_wire_experiment": {"version": 1,
            "wire_profile": "darwin-xnu-e7-arm64-bootargs1152-r2v2-v1", "source_revision": 2, "source_version": 2},
        "native_policy_experiment": {"version": 1, "producer_profile": "t8103-pinned-hv-source-facts-v1"},
        "host_input_sources": {**host_files,
            "integrations/puredarwin/m1-bringup/virtual_input_progress.py": "689a8fb0fd7728d655d3baefbee18308cb2bce7333f1c0f4a76f37bc8e5673e1",
            "integrations/puredarwin/m1-bringup/terminal_input_progress.py": "2b8901e7c29357a3440c40e1db14f5e2416f38beb71af0fee7674cb05233568c"},
        "ramdisk": {"sha256": sha(RAM), "payload_record_sha256": sha(ram_record),
            "launcher": "/usr/bin/darwin-native-private-test",
            "collector": "/usr/libexec/darwin-native-private-collector", "new_files": 2,
            "retained_files": 317, "libSystem_sha256": "9fc03c87e929310d881bd5b440bf0f3d7f0b230d79521b07cea539cf5c7e481e"},
        "capture_plan": {"host_runtime_seconds": 120, "worker_timeout_seconds": 240,
            "input": "sealed bootstrap/challenge/fixed launcher only; no HID, desktop or DATA bridge"},
        "hardware_gate": "NO_BOOT_PENDING_WHOLE_PACKET_REVIEW_AND_ROOT_DECISION",
        "scope": "Private kernel VM lifecycle self-report only; three16KiB backings, no data readback or device effect",
        "not_claimed": ["Source authentication/global exclusions", "PCIe link/chip identity/firmware",
                        "DART/MSI/DMA/Wi-Fi", "Runtime class residency, native API or boot", "GPU"],
    }
    OUT.mkdir(exist_ok=False)
    for source, name in ((KC, "kernel.macho"), (RAM, "ramdisk.img")):
        shutil.copyfile(source, OUT / name)
        os.chmod(OUT / name, 0o444)
    (OUT / "source.patch").write_text(patch)
    (OUT / "build-record.json").write_text(json.dumps(record, indent=2) + "\n")
    manifest = {"schema_version": 1, "target": "m1-mbp13-j293", "purpose": "continuing-xnu",
        "source_pins": {"puredarwin": record["source_commit"], "corecrypto": record["corecrypto_commit"],
                        "m1n1": "121fcbf35d4501814b58ca5347926a4173b6a512"}, "artifacts": {}}
    for key, name in (("kernel", "kernel.macho"), ("source_patch", "source.patch"),
                      ("build_record", "build-record.json"), ("ramdisk", "ramdisk.img")):
        path = OUT / name
        manifest["artifacts"][key] = {"path": name, "sha256": sha(path), "bytes": path.stat().st_size}
    with (OUT / "manifest.json").open("x") as stream:
        json.dump(manifest, stream, indent=2)
        stream.write("\n")
    print(json.dumps({"status": "LOCAL_PACKET_ASSEMBLED_NOT_ADMITTED_OR_BOOTED",
                      "directory": str(OUT.relative_to(LAB)), "manifest_sha256": sha(OUT / "manifest.json")}, indent=2))


if __name__ == "__main__":
    main()
