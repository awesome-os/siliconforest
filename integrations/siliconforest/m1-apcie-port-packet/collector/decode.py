#!/usr/bin/env python3
"""Original independent decoder for one complete, bounded DL31 collector block.

This accepts the collector's output only, not a filtered console transcript.
Trailing CR console translation is normalized; interior CR/noise are rejected.
No device strings are decoded and OBSERVED does not mean initialized.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

PROPERTY = 'PDApcieBridgeObservationV1'
CAP = 4096
OUTPUT_CAP = 20000
NONCE = re.compile(r'[0-9a-f]{48}\Z')
POWER_OFFSETS = (0x1b8, 0x348, 0x3e8, 0x1a0)
FIELD_WORDS = (7, 8, 9, 10, 15, 16, 17, 18, 19, 20, 11, 12, 13, 14)


def classify(raw):
    """Return exact source-consistent summary, or ValueError (no partial result)."""
    if not isinstance(raw, bytes) or len(raw) != 84:
        raise ValueError('property must be exactly 84 bytes')
    w = struct.unpack('<21I', raw)
    if w[0] != 1 or w[1] > 14 or w[2] > 5 or w[3] & ~0x3fff:
        raise ValueError('version/bounds')
    signed_result = struct.unpack('<i', raw[16:20])[0]
    reads, stage, mask, region, offset = 0, 0, 0, 5, 0

    def finish(result):
        expected = (reads, stage, mask, result, region, offset)
        if (w[1], w[2], w[3], signed_result, w[5], w[6]) != expected:
            raise ValueError('inconsistent source prefix/metadata')
        if any(w[field] for bit, field in enumerate(FIELD_WORDS) if not mask & (1 << bit)):
            raise ValueError('nonzero unread field')
        verdict, exit_code = ('OBSERVED', 0) if result == 0 else (
            ('NOT_READY', 77) if 1 <= result <= 5 else ('SOURCE_ERROR', 1))
        return dict(result=result, reads=reads, stage=stage, valid=mask,
                    region=region, offset=offset, verdict=verdict,
                    exit_code=exit_code, initialization_proven=False)

    def read(bit, r, off):
        nonlocal reads, mask, region, offset
        reads += 1
        region, offset = r, off
        if not w[3] & (1 << bit):
            return False
        mask |= 1 << bit
        return True

    def on(value):
        return value & 0xff == 0xff and value & 0x90001c00 == 0

    if not w[1]:
        if signed_result not in (-1, -2, -3):
            raise ValueError('invalid zero-read result')
        return finish(signed_result)
    stage = 1
    for i, off in enumerate(POWER_OFFSETS):
        if not read(i, 0, off):
            return finish(-3)
        if not on(w[7 + i]):
            return finish(1)
    stage = 2
    if not read(4, 2, 0):
        return finish(-3)
    if w[15] & 0x8f != 0x0f:
        result = 2
    else:
        stage = 3
        if not read(5, 1, 0x24):
            return finish(-3)
        if not w[16] & 1:
            result = 3
        else:
            stage = 4
            for bit, off in ((6, 0x800), (7, 0x814), (8, 0x804)):
                if not read(bit, 3, off):
                    return finish(-3)
            if not w[19] & 1:
                result = 4
            else:
                stage = 5
                if not read(9, 3, 0x208):
                    return finish(-3)
                result = 0
    for i, off in enumerate(POWER_OFFSETS):
        if not read(10 + i, 0, off):
            return finish(-3)
        if not on(w[11 + i]):
            return finish(5)
    return finish(result)


def _uint(s, maximum):
    if not re.fullmatch(r'0|[1-9][0-9]*', s) or len(s) > 10 or int(s) > maximum:
        raise ValueError('noncanonical/bounded unsigned integer')
    return int(s)


def _sint(s):
    if not re.fullmatch(r'0|-?[1-9][0-9]*', s) or len(s) > 11 or not -(2**31) <= int(s) < 2**31:
        raise ValueError('noncanonical/bounded signed integer')
    return int(s)


def decode(output, nonce):
    """Decode one exact block; return JSON-safe bytes/digest/exit classification.

    Transport failure blocks are valid evidence of an error, never observation.
    The caller binds the actual process exit independently to expected_exit.
    """
    if not isinstance(nonce, str) or not NONCE.fullmatch(nonce):
        raise ValueError('nonce must be lowerhex48')
    if not isinstance(output, bytes) or not output or len(output) > OUTPUT_CAP or not output.endswith(b'\n'):
        raise ValueError('output framing/cap')
    try:
        lines = [line.rstrip(b'\r').decode('ascii') for line in output[:-1].split(b'\n')]
    except UnicodeDecodeError as exc:
        raise ValueError('nonASCII') from exc
    if len(lines) > 67 or any(not line or len(line) > 512 or '\r' in line for line in lines):
        raise ValueError('line bounds/noise')
    prefix = f'{nonce} {PROPERTY}'
    result = {'nonce': nonce, 'property': PROPERTY,
              'output_sha256': hashlib.sha256(output).hexdigest(),
              'scope': 'numeric_snapshot_only_no_initialization_claim'}
    if lines[0].startswith('DL31_APCIE_COPY '):
        m = re.fullmatch(rf'DL31_APCIE_COPY {nonce} status=(missing|rpc_error) detail=(-?[0-9]+) cleanup=(-?[0-9]+)', lines[0])
        if not m or len(lines) != 2:
            raise ValueError('COPY framing')
        _sint(m[2]); _sint(m[3])
        verdict = 'MISSING' if m[1] == 'missing' else 'RPC_ERROR'
        if lines[1] != f'DL31_APCIE_VERDICT {nonce} verdict={verdict} initialization_proven=false':
            raise ValueError('COPY verdict')
        return dict(result, expected_exit=1, verdict=verdict, detail=int(m[2]),
                    cleanup=int(m[3]), raw_hex=None, summary=None)
    m = re.fullmatch(rf'DL31_APCIE_RECORD_BEGIN {prefix} bytes=([0-9]+) chunk_bytes=64', lines[0])
    if not m:
        raise ValueError('BEGIN')
    count = _uint(m[1], CAP)
    chunks = (count + 63) // 64
    if len(lines) != chunks + 3:
        raise ValueError('line/chunk count')
    raw_parts = []
    for i in range(chunks):
        m = re.fullmatch(rf'DL31_APCIE_RECORD_HEX {prefix} index=([0-9]+) ([0-9a-f]+)', lines[i + 1])
        if not m or _uint(m[1], 63) != i:
            raise ValueError('HEX nonce/index/shape')
        size = min(64, count - i * 64)
        if len(m[2]) != size * 2:
            raise ValueError('HEX length')
        raw_parts.append(bytes.fromhex(m[2]))
    raw = b''.join(raw_parts)
    digest = hashlib.sha256(raw).hexdigest()
    if lines[-2] != f'DL31_APCIE_RECORD_END {prefix} bytes={count} chunks={chunks} sha256={digest}':
        raise ValueError('END length/count/digest')
    result.update(raw_hex=raw.hex(), raw_sha256=digest, raw_bytes=len(raw))
    try:
        summary = classify(raw)
    except ValueError:
        summary = None
    verdict_line = lines[-1]
    if verdict_line == f'DL31_APCIE_VERDICT {nonce} verdict=CLEANUP_ERROR initialization_proven=false':
        return dict(result, expected_exit=1, verdict='CLEANUP_ERROR', summary=summary)
    if summary is None:
        if verdict_line != f'DL31_APCIE_VERDICT {nonce} verdict=MALFORMED initialization_proven=false':
            raise ValueError('malformed property verdict disagreement')
        return dict(result, expected_exit=1, verdict='MALFORMED', summary=None)
    expected = (f'DL31_APCIE_VERDICT {nonce} verdict={summary["verdict"]}'
                f' result={summary["result"]} reads={summary["reads"]}'
                f' stage={summary["stage"]} valid={summary["valid"]}'
                f' region={summary["region"]} offset={summary["offset"]}'
                ' initialization_proven=false')
    if verdict_line != expected:
        raise ValueError('numeric verdict disagreement')
    return dict(result, expected_exit=summary['exit_code'], verdict=summary['verdict'], summary=summary)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--nonce', required=True)
    p.add_argument('record', type=Path)
    args = p.parse_args()
    with args.record.open('rb') as f:
        raw = f.read(OUTPUT_CAP + 1)
    try:
        result = decode(raw, args.nonce)
    except ValueError as exc:
        p.exit(1, f'invalid collector record: {exc}\n')
    print(json.dumps(result, sort_keys=True))
    return result['expected_exit']


if __name__ == '__main__':
    raise SystemExit(main())
