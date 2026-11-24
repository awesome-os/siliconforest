#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare the frame MD5 lines a guest printed (AVDFF_MD5 <clip> <framemd5 line>) with the
reference <clip>.framemd5 files. Usage: compare_framemd5.py serial.log CLIP..."""
import os, re, sys

def fields(line):
    return tuple(f.strip() for f in line.split(","))

def main():
    log = open(sys.argv[1], "rb").read().decode(errors="replace")
    got = {}
    for m in re.finditer(r"AVDFF_MD5 (\S+) ([^\r\n]*)", log):
        got.setdefault(m.group(1), []).append(fields(m.group(2)))
    for m in re.finditer(r"AVDFF_(VERSION|HWACCELS|CLIP|ERR) [^\r\n]*", log):
        print(m.group(0))
    bad = 0
    for clip in sys.argv[2:]:
        name = os.path.basename(clip)
        ref = [fields(l) for l in open(clip + ".framemd5") if l.strip() and not l.startswith("#")]
        g = got.get(name, [])
        same = sum(1 for a, b in zip(ref, g) if a == b)
        first = next((i for i, (a, b) in enumerate(zip(ref, g)) if a != b), None)
        ok = len(ref) == len(g) == same and len(ref) > 0
        bad += not ok
        print("%s %s: reference %d frames, guest %d frames, identical lines %d%s" % (
            "MATCH" if ok else "MISMATCH", name, len(ref), len(g), same,
            "" if first is None else ", first difference at line %d: ref %s guest %s" % (first, ref[first], g[first])))
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
