#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# FXNET: ping output on stdin -> min/p50/p90/p99/max of the time= values
import sys,re
v=[float(m.group(1)) for l in sys.stdin for m in [re.search(r'time[=<]([0-9.]+)',l)] if m]
v.sort(); n=len(v)
if not n: print("no samples"); sys.exit()
q=lambda p: v[min(n-1,int(p*n))]
print(f"n={n} min={v[0]:.2f} p50={q(.5):.2f} p90={q(.9):.2f} p99={q(.99):.2f} max={v[-1]:.2f} mean={sum(v)/n:.2f}")
