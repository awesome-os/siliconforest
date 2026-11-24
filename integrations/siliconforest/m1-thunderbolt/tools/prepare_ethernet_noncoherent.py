#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Prepare private PCIe-C DMA buffers with a normal noncacheable CPU mapping.

Retains IOBuffer ownership, native IOMapper/IODMACommand and all refusal paths.
This is a candidate, not evidence that DMA cache policy caused a device failure.
"""
from pathlib import Path
import sys

p = Path(sys.argv[1]) / 'src/Kernel/fbsdkpi/xnu/sf_fbsd_os.cpp'
s = p.read_text()

def function(text, name):
    start = text.index(name)
    brace = text.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return start, end

def replace(text, old, new):
    if text.count(old) != 1:
        raise SystemExit('I210 noncoherent source drift: ' + old[:80])
    return text.replace(old, new)

start, end = function(s, 'sfos_malloc_contig(size_t size,')
body = replace(s[start:end],
    'kernel_task, kIODirectionInOut | kIOMemoryPhysicallyContiguous,',
    '''// XNU maps WC DRAM as normal noncacheable memory, allowing byte copies.
        // Device-type inhibit-cache mappings are unsuitable for packet copies.
        kernel_task, kIODirectionInOut | kIOMemoryPhysicallyContiguous | kIOMapWriteCombineCache,''')
s = s[:start] + body + s[end:]
start, end = function(s, 'static void *sf_owned_map(')
body = replace(s[start:end],
    'kIODirectionInOut|kIOMemoryPhysicallyContiguous,(len+page-1)&~(page-1),page);',
    'kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMapWriteCombineCache,(len+page-1)&~(page-1),page);')
body = replace(body, ' IOReturn ret=c->cmd->setMemoryDescriptor(c->owned,true);',
    ' __asm__ volatile("dsb sy" ::: "memory");\n IOReturn ret=c->cmd->setMemoryDescriptor(c->owned,true);')
body = replace(body, ' *busaddr=segment.fIOVMAddr;return c;',
    ''' device->setProperty("SFI210DMACPUCachePolicy","requested-normal-noncacheable");
 *busaddr=segment.fIOVMAddr;return c;''')
s = s[:start] + body + s[end:]
start, end = function(s, 'sfos_dma_sync(void *opaque, int to_device)')
body = s[start:end]
body = replace(body, '  if(cookie->direct){',
    '''  // Complete CPU stores to the actual normal-NC backing before the
  // command/doorbell; order device writes before any bounce copy to CPU.
  __asm__ volatile("dsb sy" ::: "memory");
  if(cookie->direct){''')
body = replace(body, '   return;\n  }',
    '   __asm__ volatile("dsb sy" ::: "memory");\n   return;\n  }')
body = replace(body, '\n  IOReturn ret=cookie->cmd->synchronize',
    '\n  __asm__ volatile("dsb sy" ::: "memory");\n  IOReturn ret=cookie->cmd->synchronize')
body = replace(body, '  if(!to_device && cookie->source->writeBytes',
    '  __asm__ volatile("dsb sy" ::: "memory");\n  if(!to_device && cookie->source->writeBytes')
s = s[:start] + body + s[end:]
p.write_text(s)
