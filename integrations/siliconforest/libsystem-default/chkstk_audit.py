#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Which functions of an arm64 Mach-O call ___chkstk_darwin (the stack probe clang emits for
frames over one page), and what each does with x0 right after the call.

Until issue 46 was fixed, the lab's libSystem returned from that call with x0 = 0. A function
is "exposed" when, after the probe, it reads x0 (stores it, uses it, or passes it to a call)
before writing it: it then ran with a zero first argument. "overwrites x0 first" functions
were unaffected.

The classification reads llvm-objdump's text, instruction by instruction, up to 40
instructions after the call: a heuristic, good for a list to look at, not a proof.

  chkstk_audit.py FILE...
"""
import re
import subprocess
import sys

STORES = ('str', 'stp', 'stur', 'strb', 'strh', 'cmp', 'cmn', 'tst', 'cbz', 'cbnz', 'tbz', 'tbnz')
X0 = re.compile(r'\b[xw]0\b')


def slots(path):
    """Addresses of the stub and GOT entries bound to ___chkstk_darwin."""
    out = subprocess.run(['llvm-objdump', '--macho', '--indirect-symbols', path], capture_output=True, text=True).stdout
    return {int(l.split()[0], 16) for l in out.splitlines() if l.strip().endswith(' ___chkstk_darwin') and l.split()[0].startswith('0x')}


def audit(path):
    want = slots(path)
    if not want:
        return None
    text = subprocess.run(['llvm-objdump', '-d', '--no-show-raw-insn', path], capture_output=True, text=True).stdout
    found, func, lines = [], None, text.splitlines()
    insn = re.compile(r'^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$')
    label = re.compile(r'^[0-9a-f]+ <(.+)>:$')
    for i, l in enumerate(lines):
        m = label.match(l)
        if m:
            func = m.group(1)
            continue
        m = insn.match(l)
        if not m:
            continue
        mnem, ops = m.group(2), m.group(3)
        hit = False
        if mnem == 'bl' and ops:
            try:
                hit = int(ops.split()[0], 16) in want
            except ValueError:
                hit = False
        elif mnem == 'blr' and i >= 2:
            # adrp xN, page ; ldr xN, [xN, #off] ; blr xN   (call through the GOT)
            a, b = insn.match(lines[i - 2]), insn.match(lines[i - 1])
            if a and b and a.group(2) == 'adrp' and b.group(2) == 'ldr':
                try:
                    page = int(a.group(3).split(',')[1].split()[0], 16)
                    off = re.search(r'#(0x[0-9a-f]+|\d+)\]', b.group(3))
                    hit = (page + (int(off.group(1), 0) if off else 0)) in want
                except (ValueError, IndexError):
                    hit = False
        if not hit:
            continue
        verdict = 'x0 not touched in 40 instructions'
        for l2 in lines[i + 1:i + 41]:
            m2 = insn.match(l2)
            if not m2:
                break
            mn, op = m2.group(2), m2.group(3).split(';')[0]
            parts = [p.strip() for p in op.split(',')]
            if mn in ('bl', 'blr', 'b', 'br', 'ret'):
                verdict = 'EXPOSED: x0 still live at the next call or branch (%s)' % mn
                break
            reads = X0.search(op) if mn in STORES else any(X0.search(p) for p in parts[1:])
            if reads:
                verdict = 'EXPOSED: reads x0 (%s %s)' % (mn, op.strip())
                break
            if parts and X0.fullmatch(parts[0] or '') and mn not in STORES:
                verdict = 'overwrites x0 first'
                break
        found.append((func, verdict))
    return found


def main():
    for path in sys.argv[1:]:
        r = audit(path)
        name = '/'.join(path.split('/')[-2:])
        if r is None:
            print('%s: does not import ___chkstk_darwin' % name)
            continue
        exposed = [f for f, v in r if v.startswith('EXPOSED')]
        print('%s: %d call sites, %d exposed' % (name, len(r), len(exposed)))
        for f, v in r:
            print('    %-90s %s' % (f[:90], v))


if __name__ == '__main__':
    main()
