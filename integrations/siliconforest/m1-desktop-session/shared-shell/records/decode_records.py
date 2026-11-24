"""Strict host parser for one supervisor-partitioned desktop-records block.

Root cuts the exact block boundaries. This parser does not search a console
and does not drop a malformed line. It is not a Shell or runtime verdict.
"""
import hashlib
import re

KINDS = {
    'session': {'session-result.txt': 16384, 'manager-status.log': 65536},
    'application': {'application-stdout.log': 8192, 'application-stderr.log': 8192},
}


def decode(raw, nonce, kind, phase):
    if not isinstance(raw, bytes) or len(raw) > 32 << 20 or not isinstance(nonce, str) or not re.fullmatch('[0-9a-f]{48}', nonce):
        raise ValueError('Invalid capture/nonce')
    if kind not in KINDS or phase not in ('live', 'closed'):
        raise ValueError('Invalid kind/phase')
    limits = KINDS[kind]
    results, current = {}, None
    lines = raw.replace(b'\r', b'').split(b'\n')
    if lines and lines[-1] == b'':
        lines.pop()
    if not lines:
        raise ValueError('Incomplete export')
    for line in lines:
        if not line.startswith(b'DL22_RECORD_'):
            raise ValueError('Malformed record')
        try:
            text = line.decode('ascii')
        except UnicodeError as error:
            raise ValueError('Non-ASCII record') from error
        begin = re.fullmatch(r'DL22_RECORD_BEGIN ([0-9a-f]{48}) (session|application) ([a-z.-]+) bytes=([0-9]+) chunk_bytes=64', text)
        data = re.fullmatch(r'DL22_RECORD_HEX ([0-9a-f]{48}) (session|application) ([a-z.-]+) index=([0-9]+) ([0-9a-f]+)', text)
        end = re.fullmatch(r'DL22_RECORD_END ([0-9a-f]{48}) (session|application) ([a-z.-]+) bytes=([0-9]+) chunks=([0-9]+) file_bytes_after=([0-9]+) sha256=([0-9a-f]{64})', text)
        absent = re.fullmatch(r'DL22_RECORD_MISSING ([0-9a-f]{48}) (session|application) ([a-z.-]+) errno=([0-9]+)', text)
        refused = re.fullmatch(r'DL22_RECORD_REFUSED ([0-9a-f]{48}) (session|application) ([a-z.-]+)', text)
        match = begin or data or end or absent or refused
        if not match or match[1] != nonce or match[2] != kind or match[3] not in limits:
            raise ValueError('Malformed, foreign or refused record')
        if absent or refused:
            raise ValueError('Missing or refused record')
        name = match[3]
        if begin:
            size = int(begin[4])
            if current or name in results or size > limits[name]:
                raise ValueError('Repeated, nested or oversized record')
            current = dict(name=name, size=size, chunks=0, data=bytearray())
        elif data:
            if not current or name != current['name'] or int(data[4]) != current['chunks']:
                raise ValueError('Missing or reordered chunk')
            expected = min(64, current['size'] - len(current['data']))
            if expected <= 0 or len(data[5]) != expected * 2:
                raise ValueError('Wrong chunk length')
            current['data'].extend(bytes.fromhex(data[5]))
            current['chunks'] += 1
        else:
            if not current or current['name'] != name:
                raise ValueError('Unexpected END')
            payload = bytes(current['data'])
            size, count, after = map(int, (end[4], end[5], end[6]))
            if (size != current['size'] or size != len(payload) or count != current['chunks'] or
                    count != (size + 63) // 64 or after < size or
                    (phase == 'closed' and after != size) or hashlib.sha256(payload).hexdigest() != end[7]):
                raise ValueError('Truncated, changed or corrupt record')
            results[name] = dict(data=payload, sha256=end[7], file_bytes_after=after)
            current = None
    if current or set(results) != set(limits):
        raise ValueError('Incomplete export')
    return results
