#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""A bench copy of sf/kernel.bin whose session ends by itself after a chosen time.

  sf_bench_kernel.py <kernel.bin> <seconds, 11..560> <output>

build.sh makes kernel-bench.bin with `sfsession_seconds=150 sfmark=1`; a bench boot that has
work to do in the running system (files sent over SSH, programs run) needs longer. This takes
the installed kernel.bin, adds the two words to the command line of its configuration record
and leaves the kernel record byte for byte. With 150 it reproduces build.sh's kernel-bench.bin
(test_kit.py checks that). The result is for the bench only: sf-bootbin-install.sh takes it
with SF_BENCH=1 and never keeps it as a previous file.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'b1'))
import pdsf_pack  # noqa: E402


def bench_copy(data, seconds):
    if not 11 <= seconds <= 560:
        raise ValueError('seconds: 11..560 (a bench boot is at most 600 s)')
    if pdsf_pack.records_offset(data) != 0:
        raise ValueError('not a record list alone (sf/kernel.bin)')
    found, end = pdsf_pack.parse(data)
    if [r[0] for r in found] != [pdsf_pack.CONFIG, pdsf_pack.KERNEL] or not all(r[4] for r in found) or end != len(data):
        raise ValueError('expected one configuration and one kernel record with good checksums and nothing else')
    (_, _, c_len, _, _, c_at), (_, k_flags, k_len, k_raw, _, k_at) = found
    lines = data[c_at:c_at + c_len].decode('ascii').split('\n')
    if not lines[0].startswith('bootargs=') or 'sfsession_seconds=' in lines[0] or 'sfmark=' in lines[0]:
        raise ValueError('the configuration is not the installed one (it already ends its session, or has no bootargs line)')
    lines[0] += ' sfsession_seconds=%d sfmark=1' % seconds
    config = '\n'.join(lines).encode('ascii')
    if len(lines[0]) - len('bootargs=') >= 1024:
        raise ValueError('the command line would be too long')
    return (pdsf_pack.record(pdsf_pack.CONFIG, config) + pdsf_pack.record(pdsf_pack.KERNEL, data[k_at:k_at + k_len], k_raw, k_flags)
            + pdsf_pack.record(pdsf_pack.END))


if __name__ == '__main__':
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    out = bench_copy(Path(sys.argv[1]).read_bytes(), int(sys.argv[2]))
    Path(sys.argv[3]).write_bytes(out)
    print('%s: %d bytes' % (sys.argv[3], len(out)))
