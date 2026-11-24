#!/usr/bin/env python3
"""Strict offline receipt decoder. A ready observation is not physical input."""
from __future__ import annotations
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

SIZE = 704
DESCRIPTOR_SHA256 = "4ae5bc2626bff5458ba2325e29614f1fec5e8c5a4d32c396f64d59508ec77609"

def u32(raw: bytes, off: int) -> int:
    return struct.unpack_from("<I", raw, off)[0]

def u64(raw: bytes, off: int) -> int:
    return struct.unpack_from("<Q", raw, off)[0]

def classify(raw: bytes, descriptor_sha256: str = DESCRIPTOR_SHA256) -> int:
    """Independent Python policy; optional digest is for synthetic host tests only."""
    if len(raw) != SIZE or u32(raw, 0) != 1 or u32(raw, 4) != SIZE:
        return 1
    if any(u32(raw, off) for off in (12, 20, 24)):
        return 1
    n = u32(raw, 32)
    if n > 2 or u32(raw, 36) != 1 or u32(raw, 44) != 1 or u32(raw, 40):
        return 1
    if any(u32(raw, off) != 1 for off in range(56, 72, 4)):
        return 1
    identities: dict[bytes, int] = {}
    for i in range(n):
        name = raw[104 + i * 128:232 + i * 128]
        if b"\0" not in name:
            return 1
        name = name.split(b"\0", 1)[0]
        ident = u64(raw, 488 + i * 8)
        if (name not in (b"SFM1SPIHID", b"SFM1SPITrackpad") or name in identities
                or not ident or ident in identities.values()):
            return 1
        identities[name] = ident
    if len(identities) != 2:
        return 77
    parent, child = identities[b"SFM1SPIHID"], identities[b"SFM1SPITrackpad"]
    if u64(raw, 80) != parent or u64(raw, 88) != child:
        return 1
    if u32(raw, 48) != 1 or u32(raw, 52) != 1 or u32(raw, 72) != 1 or u32(raw, 76) != 1:
        return 1
    if u64(raw, 96) != parent or u64(raw, 512) != parent:
        return 1
    if [u32(raw, off) for off in (28, 528, 532, 536)] != [1, 110, 4, 32]:
        return 1
    if hashlib.sha256(raw[548:658]).hexdigest() != descriptor_sha256 or u32(raw, 658) != 2:
        return 1
    version, size, commit, route, fault, stop, error, reserved = struct.unpack_from("<8I", raw, 662)
    if version != 1 or size != 32 or reserved or any(v > 1 for v in (commit, route, fault, stop)):
        return 1
    if ((route and (not commit or fault or stop or error)) or (fault and not error)
            or (error and not fault and not stop)):
        return 1
    nodes = [u32(raw, 540), u32(raw, 544)]
    if any(v > 1 for v in nodes):
        return 1
    return 0 if commit and route and not (fault or stop or error) and nodes == [1, 1] else 77

def unique_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result

def decode(console: bytes, nonce: str) -> dict:
    if not re.fullmatch("[0-9a-f]{48}", nonce):
        raise ValueError("bad nonce")
    # CR removal is the only transport normalization; no warning filtering.
    lines = console.replace(b"\r", b"").splitlines()
    indices = [i for i, line in enumerate(lines) if line.startswith(b"DL16_GATE_BEGIN_")]
    if len(indices) != 1:
        raise ValueError("expected exactly one gate export")
    start = indices[0]
    expected_begin = f"DL16_GATE_BEGIN_{nonce} bytes=704 physical_input_proven=false device_opened=false".encode()
    if lines[start] != expected_begin:
        raise ValueError("unexpected gate begin")
    block = lines[start:start+14]
    if len(block) != 14:
        raise ValueError("incomplete gate export")
    summary = json.loads(block[1], object_pairs_hook=unique_keys)
    raw = bytearray()
    for i in range(11):
        prefix = f"DL16_GATE_HEX_{nonce} index={i} ".encode()
        line = block[i+2]
        if not line.startswith(prefix) or not re.fullmatch(b"[0-9a-f]{128}", line[len(prefix):]):
            raise ValueError("gate chunk order/size/hex mismatch")
        raw.extend(bytes.fromhex(line[len(prefix):].decode("ascii")))
    raw = bytes(raw)
    end = re.fullmatch(
        f"DL16_GATE_END_{nonce} chunks=11 bytes=704 sha256=([0-9a-f]{{64}}) result=(0|1|77)".encode(), block[13])
    if not end or hashlib.sha256(raw).hexdigest().encode() != end[1]:
        raise ValueError("gate digest or footer mismatch")
    if raw[694:] != bytes(10):
        raise ValueError("nonzero reserved bytes")
    result = classify(raw)  # always the acquired descriptor digest here
    if u32(raw, 8) != result or int(end[2]) != result:
        raise ValueError("guest/offline verdict mismatch")
    wanted = {
        "event": "DL16_GATE", "nonce": nonce, "result": result,
        "reason": u32(raw, 12), "detail": struct.unpack_from("<i", raw, 16)[0],
        "query_errors": u32(raw, 20), "release_errors": u32(raw, 24),
        "hid_count": u32(raw, 32), "physical_input_proven": False,
        "device_opened": False, "non_atomic_observation": True,
    }
    # Strict types as well as values (Python's bool == int is not enough).
    if (summary.keys() != wanted.keys() or any(type(summary[k]) is not type(v)
                                               or summary[k] != v for k, v in wanted.items())):
        raise ValueError("summary does not match private bytes")
    if sum(line.startswith(b"DL16_GATE_HEX_") for line in lines) != 11 or sum(
            line.startswith(b"DL16_GATE_END_") for line in lines) != 1:
        raise ValueError("duplicate or stray gate records")
    return {"summary": wanted, "raw": raw, "sha256": end[1].decode(),
            "descriptor_sha256": hashlib.sha256(raw[548:658]).hexdigest(),
            "state_hex": raw[662:694].hex(), "interface_hex": raw[658:662].hex()}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("console", type=Path)
    parser.add_argument("--nonce", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    result = decode(args.console.read_bytes(), args.nonce)
    args.out.mkdir(mode=0o700, parents=True, exist_ok=False)
    (args.out / "registry-observation.bin").write_bytes(result.pop("raw"))
    (args.out / "decoded.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result["summary"], sort_keys=True))

if __name__ == "__main__":
    main()
