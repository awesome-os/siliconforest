#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run the actual prepared allocator against explicit native allocation fixtures.

Argument: the native noncoherent SDK source. Checks requested CPU mapping
policy, complete owned pages and failure cleanup, not actual ARM page tables.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]) / 'src/Kernel/fbsdkpi/xnu/sf_fbsd_os.cpp'
s = source.read_text()
start = s.index('sfos_malloc_contig(size_t size,')
brace = s.index('{', start)
end, depth = brace + 1, 1
while depth:
    depth += (s[end] == '{') - (s[end] == '}')
    end += 1
actual = 'void *\n' + s[start:end]
fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
using mach_vm_address_t=uint64_t;
static constexpr size_t PAGE_SIZE=0x4000;
static constexpr unsigned kIODirectionInOut=3,kIOMemoryPhysicallyContiguous=0x10,kIOMapWriteCombineCache=0x20,kIOMemoryMapperNone=0;
static constexpr int kernel_task=0,kIOReturnSuccess=0;
static unsigned optionsSeen,releases,completions;
static size_t bytesSeen;static uint64_t maskSeen;
static bool allocationFail,prepareFail,metadataFail;
struct IOBufferMemoryDescriptor {
 std::vector<unsigned char> data;
 explicit IOBufferMemoryDescriptor(size_t n):data(n,0xcc){}
 static IOBufferMemoryDescriptor *inTaskWithPhysicalMask(int,unsigned options,size_t size,uint64_t mask){
  optionsSeen=options;bytesSeen=size;maskSeen=mask;
  return allocationFail?nullptr:new IOBufferMemoryDescriptor(size);
 }
 void *getBytesNoCopy(){return data.data();}size_t getLength(){return data.size();}
 int prepare(){return prepareFail?1:0;}void complete(){++completions;}
 void release(){++releases;delete this;}
 uint64_t getPhysicalSegment(unsigned,void *,unsigned){return 0x812340000;}
};
struct sf_mapping {void *va;IOBufferMemoryDescriptor *buffer;};
static sf_mapping *recorded;
static void *IOMalloc(size_t size){return metadataFail?nullptr:malloc(size);}
static void mapping_add(sf_mapping *m){assert(!recorded);recorded=m;}
ACTUAL
int main(){
 uint64_t phys=0;
 assert(!sfos_malloc_contig(0,1,0,&phys));
 assert(!sfos_malloc_contig(UINT64_MAX,1,0,&phys));
 void *p=sfos_malloc_contig(4096,64,0,&phys);
 assert(p && recorded && p==recorded->buffer->getBytesNoCopy());
 assert(bytesSeen==0x4000 && maskSeen==(0xfffffffffULL & ~0x3fffULL));
 assert(optionsSeen==(kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMapWriteCombineCache));
 for(size_t i=0;i<bytesSeen;++i)assert(((unsigned char *)p)[i]==0);
 assert(phys==0x812340000);recorded->buffer->release();free(recorded);recorded=nullptr;
 allocationFail=true;assert(!sfos_malloc_contig(4096,0x4000,0,&phys));allocationFail=false;
 prepareFail=true;unsigned before=releases;assert(!sfos_malloc_contig(4096,0x4000,0,&phys));assert(releases==before+1 && !recorded);prepareFail=false;
 metadataFail=true;before=completions;assert(!sfos_malloc_contig(4096,0x4000,0,&phys));assert(completions==before+1 && !recorded);
}
'''.replace('ACTUAL', actual)
with tempfile.TemporaryDirectory() as temporary:
    temporary = Path(temporary)
    (temporary / 'test.cpp').write_text(fixture)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', '-g',
                    '-fsanitize=address,undefined', str(temporary / 'test.cpp'),
                    '-o', str(temporary / 'test')], check=True)
    subprocess.run([str(temporary / 'test')], check=True)
print('actual allocator requests normal-NC complete pages, zeroes padding, retains allocation identity and cleans failure paths PASS (host fixtures only)')
