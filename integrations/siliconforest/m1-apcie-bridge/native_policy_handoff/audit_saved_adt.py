"""Bounded historical metadata replay only; never a runtime handoff verdict."""
from pathlib import Path
import hashlib
import json
import struct

import handoff as h

ROOT = Path(__file__).resolve().parents[4]
PATH = ROOT / "runs/m1-desktop-010/apcie-port-attempt-001/guest/guest-adt.bin"
SHA = "17e4be202fa53a20bee179ff8eccaeb7a077fa78d26dbe167517cc2cccdba4c0"
raw = PATH.read_bytes()
assert len(raw) <= h.ADT_LIMIT and hashlib.sha256(raw).hexdigest() == SHA
nodes = h._parse(raw)
chosen = nodes["/chosen"]
assert not any(p.key == h.PROPERTY for p in chosen.properties)
value = b"SYNTHETIC_SPLICE_AUDIT_NOT_A_FACTS_WIRE"
addition = h.PROPERTY.encode().ljust(32, b"\0") + struct.pack("<I", len(value)) + value
addition += b"\0" * ((-len(value)) & 3)
inserted = bytearray(raw[:chosen.properties_end] + addition + raw[chosen.properties_end:])
struct.pack_into("<I", inserted, chosen.begin, chosen.property_count + 1)
serialized = bytes(inserted)
assert h.canonical_base(serialized) == raw
assert h._parse(serialized)["/chosen"].get(h.PROPERTY).value == value
print(json.dumps(dict(status="SAVED_ADT_PARSER_AND_SPLICE_REPLAY_ONLY",
                     source=str(PATH.relative_to(ROOT)), source_sha256=SHA,
                     source_bytes=len(raw), nodes=len(nodes),
                     properties=sum(n.property_count for n in nodes.values()),
                     roundtrip_byte_exact=True, final_bytes=len(serialized),
                     synthetic_payload_valid_facts=False,
                     actual_native_wire_available=False, runtime_handoff_qualified=False,
                     raw_device_values_printed=False), sort_keys=True))
