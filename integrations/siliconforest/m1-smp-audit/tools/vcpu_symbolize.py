#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Names the kernel addresses of the SF_VCPU lines that host/vcpu-dump.patch writes to a
# boot's guest/worker.log (one line per guest processor when the capture stops).
#   vcpu_symbolize.py <kernel image, e.g. .../System/Library/Kernels/kernel.development.t8103> <worker.log>
# The load slide is found from a processor that sits in the idle thread (its chain ends
# processor_idle, idle_thread, Call_continuation). Addresses outside the kernel's text are
# kexts: name those with ../../m1-dcp/tools/symbolize.sh and the kext's load address.
import bisect, json, subprocess, sys
NM = '/nix/store/c17dkymcla7drga7i6l4qpppfffwz54g-darwin-cross-toolchain-nix/bin/arm64-apple-darwin20.4-nm'
syms = []
for line in subprocess.run([NM, '-n', sys.argv[1]], capture_output=True, text=True).stdout.splitlines():
    p = line.split(None, 2)
    if len(p) == 3 and p[1] in 'tTsS':
        syms.append((int(p[0], 16), p[2].strip()))
syms.sort()
addrs = [a for a, _ in syms]
names = {n: a for a, n in syms}
def look(a):
    i = bisect.bisect_right(addrs, a) - 1
    return (syms[i][1], a - syms[i][0]) if i >= 0 else ('?', 0)
recs = [json.loads(l.split('SF_VCPU ', 1)[1]) for l in open(sys.argv[2], errors='replace') if 'SF_VCPU {' in l]
slide = None
for r in recs:
    f = [int(x, 16) for x in r['frames']]
    if len(f) < 3:
        continue
    base = (f[-3] - names['_processor_idle']) & ~0x3fff
    for c in range(base - 0x40000, base + 0x44000, 0x4000):
        if look(f[-3] - c)[0] == '_processor_idle' and look(f[-2] - c)[0] == '_idle_thread':
            slide = c
    if slide is not None:
        break
if slide is None:
    sys.exit('no processor in the idle thread: the slide cannot be derived from this dump')
print('slide', hex(slide))
def name(x):
    a = int(x, 16) - slide
    if a < addrs[0] or a > addrs[-1] + 0x8000:
        return x + ' (not kernel text)'
    n, o = look(a)
    return f'{n}+{o:#x}'
for r in recs:
    print(f"cpu {r['cpu']} spsr={r['spsr']} far={r['far']} x0={r['x0']} x1={r['x1']}")
    print('   pc ', name(r['elr']))
    print('   lr ', name(r['lr']))
    for x in r['frames']:
        print('      ', name(x))
