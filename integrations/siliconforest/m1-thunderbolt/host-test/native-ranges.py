#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise actual pinned IOPCIRange code against this host's declarations.

Pass the actual frozen native SDK source tree. This is an allocator host
regression, not PCI configuration execution or hardware qualification.
"""
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

base = Path(sys.argv[1]) / 'src/Kernel/Extensions/IOPCIFamily'
original = (base / 'IOPCIRange.cpp').read_text()
header = (base / 'include/IOKit/pci/IOPCIConfigurator.h').read_text()
start = original.index('IOPCIScalar IOPCIScalarAlign(')
end = original.index('\n#ifndef KERNEL\n\nint main(', start)
# Keep the actual Apple license and function bodies; replace only host-only
# framework includes and the upstream standalone test main in this temp copy.
code = original[:original.index('#ifdef KERNEL')] + '''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "ranges.h"
#define panic(x) do { fprintf(stderr,"panic: %s\\n",x); abort(); } while(0)
#define kprintf(...) do {} while(0)
''' + original[start:end]
host = (Path(__file__).parents[1] / 'kernel/SFCIOPCIHost.cpp').read_text()
configure = host.split('bool SFCIOPCIHost::configure(', 1)[1].split('IODeviceMemory *', 1)[0]
ranges = re.findall(r'addBridge(Memory|PrefetchableMemory)Range\((0x[0-9a-f]+)ULL,(0x[0-9a-f]+)ULL', configure)
assert len(ranges) == 3
declarations = '\n'.join(f'assert(IOPCIRangeListAddRange(&{"memory" if kind == "Memory" else "prefetch"},{0 if kind == "Memory" else 1},{start}ULL,{size}ULL));' for kind, start, size in ranges)
harness = r'''
static void maximize(IOPCIRange *head,IOPCIRange *request)
{
 IOPCIRangeInit(request,1,0,0x100000,0x100000);
 request->size=0x100000;request->proposedSize=UINT64_MAX;
 request->flags=kIOPCIRangeFlagMaximizeRoot;
 assert(IOPCIRangeListAllocateSubRange(head,request));
}
static void clear(IOPCIRange *head)
{
 while(head){auto next=head->next;IOPCIRangeFree(head);head=next;}
}
int main()
{
 IOPCIRange *bad=NULL,badPF={},badMEM={};
 assert(IOPCIRangeListAddRange(&bad,0,0x00100000,0x3ff00000));
 assert(IOPCIRangeListAddRange(&bad,0,0x40000000,0x40000000));
 assert(IOPCIRangeListAddRange(&bad,0,0x580000000ULL,0x80000000));
 maximize(bad,&badPF);
 assert(badPF.start==0x00100000 && badPF.end==0x80000000);
 IOPCIRangeInit(&badMEM,0,0,0x1000000,0x100000);
 assert(!IOPCIRangeListAllocateSubRange(bad,&badMEM));
 clear(bad);
 IOPCIRange *memory=NULL,*prefetch=NULL;
 DECLARATIONS
 assert(memory && prefetch);
 IOPCIRange pf={},root={},upstream={},downstream={},bar0={},msix={};
 maximize(prefetch,&pf);
 assert(pf.start==0x40000000 && pf.end==0x80000000);
 IOPCIRangeInit(&root,0,0,0x1000000,0x100000);
 assert(IOPCIRangeListAllocateSubRange(memory,&root));
 assert(root.start>=0x00100000 && root.end<=0x40000000);
 IOPCIRangeInit(&upstream,0,0,0x1000000,0x100000);
 assert(IOPCIRangeListAllocateSubRange(&root,&upstream));
 IOPCIRangeInit(&downstream,0,0,0x100000,0x100000);
 assert(IOPCIRangeListAllocateSubRange(&upstream,&downstream));
 IOPCIRangeInit(&bar0,0,0,0x80000,0x80000);
 IOPCIRangeInit(&msix,0,0,0x4000,0x4000);
 assert(IOPCIRangeListAllocateSubRange(&downstream,&bar0));
 assert(IOPCIRangeListAllocateSubRange(&downstream,&msix));
 assert(bar0.start>=downstream.start && msix.end<=downstream.end);
 assert(bar0.end<=msix.start || msix.end<=bar0.start);
 clear(memory);clear(prefetch);
 puts("native IOPCIRange: old prefetch starvation reproduced; actual host declarations permit nested MEM + I210 BAR/MSI-X allocations PASS (host only)");
}
'''.replace('DECLARATIONS', declarations)
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    (temp / 'ranges.h').write_text(header)
    (temp / 'test.cpp').write_text(code + harness)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++14', '-O1', '-g',
                    '-fsanitize=address,undefined', str(temp / 'test.cpp'), '-o', str(temp / 'test')], check=True)
    subprocess.run([str(temp / 'test')], check=True)
print('actual native range source SHA256', hashlib.sha256(original.encode()).hexdigest())
