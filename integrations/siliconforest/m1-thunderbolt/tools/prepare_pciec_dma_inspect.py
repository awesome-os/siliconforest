#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Apply only diagnostic methods to an exact frozen PCIe-C mapper source.

This deliberately does not copy the current mapper's USB registration changes.
"""
from pathlib import Path
import re
import sys

dest, reference = map(Path, sys.argv[1:])
src = reference.read_text()
match = re.search(r'IOReturn SFCIOPCIDartMapper::inspectDMA\([^\n]+\)\s*\{', src)
assert match
end, depth = match.end(), 1
while depth:
    depth += (src[end] == '{') - (src[end] == '}')
    end += 1
method = src[match.start():end]

def replace(path, old, new):
    text = path.read_text()
    if text.count(old) != 1:
        raise SystemExit('PCIe-C DMA diagnostic source drift: ' + old[:80])
    path.write_text(text.replace(old, new))

p = dest / 'SFCIOPCIDartMapper.cpp'
replace(p, '#include "sf_usb_iova.h"',
        '#include "sf_usb_iova.h"\n#include "sf_pciec_dma_inspect.h"\n#include "sf_cpu_par.h"')
replace(p, 'off==0x20 || off==0x40 || off==0x60',
        'off==0x20 || off==0x40 || off==0x50 || off==0x54 || off==0x60')
replace(p, ' if(!name || !name->isEqualTo("SFCIOPCIDMAReady"))',
        ''' if(name && name->isEqualTo(SF_PCIEC_DMA_INSPECT_FUNCTION)){
  if(!p1 || !p2 || p3 || p4 || wait)return kIOReturnBadArgument;
  return inspectDMA((IOService *)p1,p2);
 }
 if(!name || !name->isEqualTo("SFCIOPCIDMAReady"))''')
replace(p, 'IOReturn SFCIOPCIDartMapper::iovmMapMemory(',
        method + '\nIOReturn SFCIOPCIDartMapper::iovmMapMemory(')
p = dest / 'sf_cio_pci_mapper.h'
replace(p, ' bool live(); // Called with mapper and controller locks held.',
        ' bool live(); // Called with mapper and controller locks held.\n IOReturn inspectDMA(IOService *,void *);')
