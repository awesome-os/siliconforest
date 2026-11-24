"""Strict host parser for one read-only terminal-records export per capture."""
import hashlib
import re

LIMITS = {'pty-rx.bin': 262144, 'pty-tx.bin': 16384, 'child.json': 4096,
          'child-ready.json': 4096, 'grid-final.txt': 16384, 'result.json': 16384}
REQUIRED_LIVE = set(LIMITS) - {'grid-final.txt', 'result.json'}


def decode(raw, nonce, phase='live'):
    if not isinstance(raw, bytes) or len(raw) > 32 << 20 or not isinstance(nonce, str) or not re.fullmatch('[0-9a-f]{48}', nonce):
        raise ValueError('Invalid capture/nonce')
    if phase not in ('live', 'closed'):
        raise ValueError('Invalid phase')
    results, missing, current = {}, set(), None
    prefix = f'DL11_RECORD_'.encode()
    for line in raw.replace(b'\r', b'').split(b'\n'):
        if not line.startswith(prefix):
            continue
        try:
            text = line.decode('ascii')
        except UnicodeError as error:
            raise ValueError('Non-ASCII record') from error
        begin = re.fullmatch(r'DL11_RECORD_BEGIN ([0-9a-f]{48}) ([a-z.-]+) bytes=([0-9]+) chunk_bytes=64', text)
        data = re.fullmatch(r'DL11_RECORD_HEX ([0-9a-f]{48}) ([a-z.-]+) index=([0-9]+) ([0-9a-f]+)', text)
        end = re.fullmatch(r'DL11_RECORD_END ([0-9a-f]{48}) ([a-z.-]+) bytes=([0-9]+) chunks=([0-9]+) file_bytes_after=([0-9]+) sha256=([0-9a-f]{64})', text)
        absent = re.fullmatch(r'DL11_RECORD_MISSING ([0-9a-f]{48}) ([a-z.-]+) errno=2', text)
        match = begin or data or end or absent
        if not match or match[1] != nonce or match[2] not in LIMITS:
            raise ValueError('Malformed, foreign or refused record')
        name = match[2]
        if begin:
            size = int(begin[3])
            if current or name in results or name in missing or size > LIMITS[name]:
                raise ValueError('Repeated, nested or oversized record')
            current = dict(name=name, size=size, chunks=0, data=bytearray())
        elif data:
            if not current or name != current['name'] or int(data[3]) != current['chunks']:
                raise ValueError('Missing or reordered chunk')
            expected = min(64, current['size'] - len(current['data']))
            if expected <= 0 or len(data[4]) != expected * 2:
                raise ValueError('Wrong chunk length')
            current['data'].extend(bytes.fromhex(data[4]))
            current['chunks'] += 1
        elif end:
            if not current or current['name'] != name:
                raise ValueError('Unexpected END')
            payload = bytes(current['data'])
            size, count, after = map(int, (end[3], end[4], end[5]))
            if (size != current['size'] or size != len(payload) or count != current['chunks'] or
                    count != (size + 63) // 64 or after < size or
                    (phase == 'closed' and after != size) or hashlib.sha256(payload).hexdigest() != end[6]):
                raise ValueError('Truncated, changed or corrupt record')
            results[name] = dict(data=payload, sha256=end[6], file_bytes_after=after)
            current = None
        else:
            if current or name in results or name in missing or phase == 'closed' or name in REQUIRED_LIVE:
                raise ValueError('Missing required or duplicate record')
            missing.add(name)
    if current or set(results) | missing != set(LIMITS):
        raise ValueError('Incomplete export')
    return results
