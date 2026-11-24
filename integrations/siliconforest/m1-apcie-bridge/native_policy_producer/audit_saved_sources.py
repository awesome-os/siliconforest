"""Historical partial-source audit only; never construct missing wire/samples."""
from pathlib import Path
import hashlib
import json

import producer as p

ROOT = Path(__file__).resolve().parents[4]
DIRECTORY = ROOT / "runs/m1-desktop-010/apcie-port-attempt-001/guest"
FILES = {
    "guest-adt.bin": "17e4be202fa53a20bee179ff8eccaeb7a077fa78d26dbe167517cc2cccdba4c0",
    "original-bootargs.bin": "4309a121450467f35f3a004e04f9e25f965f74eecac63419c989a5df1ee87d03",
    "mcc-carveouts.bin": "68e85adc1853aec128971d18379fa7e9bd1a1f016edc6f2a42274bb06981a559",
}
raw = {}
for name, digest in FILES.items():
    raw[name] = (DIRECTORY / name).read_bytes()
    assert len(raw[name]) <= 2 << 20 and hashlib.sha256(raw[name]).hexdigest() == digest
module = p._module("adt_source_inventory/inventory.py", p.INVENTORY_SHA256, "_producer_saved_inventory")
inventory = module.extract(raw["guest-adt.bin"], profile=module.PROFILE)
original, video = p._original(raw["original-bootargs.bin"], p.ORIGINAL_PROFILE)
dram = p.facts.Range(inventory.dram.base, inventory.dram.bytes)
vram = p.facts.Range(inventory.vram.base, inventory.vram.bytes)
assert p._contains(dram, original) and p._contains(dram, vram) and p._contains(vram, video.visible)
assert len(raw["mcc-carveouts.bin"]) == 80
context = p.Context(bytes([1]) * 24, bytes([2]) * 32, bytes([3]) * 32, p.HV_SHA256)
try:
    p.measure(raw["guest-adt.bin"], raw["original-bootargs.bin"], raw["mcc-carveouts.bin"], context,
              profile=p.PROFILE, original_profile=p.ORIGINAL_PROFILE)
except ValueError as error:
    assert "exact384" in str(error)
else:
    raise AssertionError("Historical compact MCC input was accepted")
print(json.dumps(dict(status="HISTORICAL_PARTIAL_SOURCE_AUDIT_ONLY_NO_HANDOFF",
                     inputs_sha256=FILES, inventory_records=inventory.metadata()["projected_source_records"],
                     original_common88_valid=True, full_declared_VRAM_contains_visible=True,
                     old80_MCC_refused=True, full384_MCC_available=False,
                     final_native_wire_available=False, missing_data_reconstructed=False,
                     image_context_labels="synthetic refusal test only",
                     runtime_or_old_grade_changed=False, raw_source_values_printed=False), sort_keys=True))
