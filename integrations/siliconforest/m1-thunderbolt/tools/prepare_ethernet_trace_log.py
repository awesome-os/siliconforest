#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Keep finite I210 diagnostics visible without global FreeBSD debug logging."""
from pathlib import Path
import sys
root=Path(sys.argv[1])
p=root/'src/Kernel/Extensions/SFFBSDem/dev/e1000/igb_txrx.c'
s=p.read_text()
def replace(old,new):
    global s
    if s.count(old)!=1:
        raise SystemExit('I210 trace-log source drift: '+old[:80])
    s=s.replace(old,new)
if s.count('printf("SFI210TRACE ')!=4:
    raise SystemExit('I210 trace-log requires four original trace sites')
s = s.replace('printf("SFI210TRACE ', 'sf_i210_trace_log("SFI210TRACE ')
assert s.count('sf_i210_trace_log("SFI210TRACE ') == 4
p.write_text(s)

# The IOKit/FreeBSD handoff precedes descriptor generation. Record bounded
# admission and results here too, so an absent encap is not blamed on DMA.
shim = root / 'src/Kernel/fbsdkpi'
for relative, include, before, after in [
    ('xnu/SFFBSDEthernet.cpp', '#include "sf_fbsd_attach.h"',
     '\tint error = sf_fbsd_transmit(fIfp, fTxBuffer, len);',
     '''\tint error = sf_fbsd_transmit(fIfp, fTxBuffer, len);
    static uint32_t sf_trace_output;
    if (sf_i210_trace_take(&sf_trace_output))
        sf_i210_trace_log("SFI210TRACE output len=%u error=%d enabled=%u\\n",
            (unsigned)len, error, (unsigned)fEnabled);'''),
    ('pd/sf_driver.c', '#include "sf_fbsd_attach.h"',
     '\tIFF_UNLOCKGIANT(ifp);\n\treturn result;',
     '''\tIFF_UNLOCKGIANT(ifp);
    static uint32_t sf_trace_transmit;
    if (sf_i210_trace_take(&sf_trace_transmit))
        sf_i210_trace_log("SFI210TRACE transmit len=%u result=%d flags=%x drvflags=%x\\n",
            (unsigned)len, result, (unsigned)ifp->if_flags,
            (unsigned)ifp->if_drv_flags);
\treturn result;'''),
]:
    p = shim / relative
    s = p.read_text()
    replace(include, include + '\n#include "sf_i210_trace.h"')
    replace(before, after)
    p.write_text(s)
