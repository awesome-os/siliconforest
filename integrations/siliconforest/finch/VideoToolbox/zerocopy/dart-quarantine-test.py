#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile extracted AVD DART/cleanup bodies against ownership/MMIO host stubs.

These injected failures check logical ownership and allocator bookkeeping only.
They do not establish DMA quiescence, cache coherence, task pinning or hardware
behavior. No target is contacted. Production functions are extracted afresh;
the sole body transformation replaces ARM's dsb sy with a host compiler fence.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def balanced(text, opening):
    """Find the closing brace while ignoring C strings and comments."""
    tokens = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)
    depth = 0
    for token in tokens.finditer(text, opening):
        if token[0] == '{':
            depth += 1
        elif token[0] == '}':
            depth -= 1
            if not depth:
                return token.end()
    raise ValueError('Unterminated source body')


def extract(text, name):
    # Definitions use a return-type line followed by the function name.
    pattern = re.compile(r'(?m)^(?:extern "C"\s+)?(?:static\s+)?(?:[A-Za-z_]\w*(?:\s+|\s*\*\s*))+\n?' + re.escape(name) + r'\s*\(')
    matches = list(pattern.finditer(text))
    if len(matches) != 1:
        raise ValueError(f'Expected one definition of {name}, found {len(matches)}')
    start = matches[0].start()
    opening = text.index('{', matches[0].end())
    return text[start:balanced(text, opening)]


def structure(text, name):
    found = re.search(r'(?m)^struct ' + re.escape(name) + r'\s*{', text)
    if not found:
        raise ValueError('Missing structure ' + name)
    end = balanced(text, text.index('{', found.start()))
    if text[end] != ';':
        raise ValueError('Missing structure terminator')
    return text[found.start():end + 1]


STUBS = r'''
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <vector>
#include <string>
#include <algorithm>
#include <set>
#define IOLog(...) ((void)0)
using IOByteCount=uint64_t;
using IODirection=unsigned;
using IOReturn=int;
using IOOptionBits=unsigned;
static constexpr unsigned kIODirectionInOut=3,kIOMemoryPhysicallyContiguous=4;
static constexpr int kIOReturnSuccess=0,kIOMemoryMapperNone=0;
static void *kernel_task=nullptr;
struct IOLock {};
static void IOLockLock(IOLock*) {}
static void IOLockUnlock(IOLock*) {}
static void IODelay(unsigned) {}
static unsigned flushes=0,allocationCalls=0,mapReleases=0,mapUnmaps=0;
static unsigned descriptorCompletes=0,descriptorReleases=0,cookieFrees=0;
static bool failInvalidate=false;
static size_t nextBadPage=SIZE_MAX;
static std::vector<std::string> events;
struct SFMmio {
 uint32_t r32(unsigned) const {return failInvalidate?4:0;}
 void w32(unsigned offset,uint32_t) const {
  if(offset==0x20) {flushes++;events.push_back("invalidate");}
 }
};
static std::set<void*> cookieAllocations;
static void *IOMalloc(size_t n) {void *p=std::calloc(1,n);if(p)cookieAllocations.insert(p);return p;}
static void IOFree(void *p,size_t) {assert(cookieAllocations.erase(p)==1);cookieFrees++;std::free(p);}
struct IOMemoryDescriptor {
 size_t length,badPage=SIZE_MAX;
 uint64_t physical;
 unsigned completes=0,releases=0;
 void *data;
 IOMemoryDescriptor(size_t n,uint64_t pa):length(n),physical(pa) {
  data=std::aligned_alloc(16384,(n+16383)&~size_t(16383));assert(data);
 }
 virtual ~IOMemoryDescriptor() {std::free(data);}
 IOByteCount getLength() const {return length;}
 uint64_t getPhysicalSegment(IOByteCount off,IOByteCount *n,int) {
  if(n)*n=length-off;
  return off==badPage?0:physical+off;
 }
 IOReturn prepare(IODirection=3) {return 0;}
 IOReturn complete(IODirection=3) {completes++;descriptorCompletes++;events.push_back("complete");return 0;}
 void release() {releases++;descriptorReleases++;events.push_back("descriptor-release");}
 void *getBytesNoCopy() {return data;}
};
static std::vector<IOMemoryDescriptor*> descriptors;
static uint64_t nextPhysical=0x10000000;
struct IOBufferMemoryDescriptor:IOMemoryDescriptor {
 using IOMemoryDescriptor::IOMemoryDescriptor;
 static IOBufferMemoryDescriptor *inTaskWithPhysicalMask(void*,unsigned flags,size_t n,uint64_t) {
  allocationCalls++;
  auto *p=new IOBufferMemoryDescriptor(n,nextPhysical);nextPhysical+=(n+16383)&~size_t(16383);
  if(!(flags&kIOMemoryPhysicallyContiguous)){p->badPage=nextBadPage;nextBadPage=SIZE_MAX;}
  descriptors.push_back(p);return p;
 }
};
struct IOMemoryMap {
 unsigned unmaps=0,releases=0;
 void unmap(){unmaps++;mapUnmaps++;events.push_back("kernel-unmap");}
 void release(){releases++;mapReleases++;events.push_back("map-release");}
};
static std::vector<IOMemoryMap*> maps;
'''


TESTS = r'''
static void setup() {
 gDart.attached=true;gDart.pages=128;
 gDart.bitmap=(uint8_t*)std::calloc(gDart.pages/8,1);
 gDart.l1=(uint64_t*)std::calloc(2048,sizeof(uint64_t));
}
static DmaCookie *makeCookie(bool mapped=true,bool prepared=true) {
 auto *c=(DmaCookie*)IOMalloc(sizeof(DmaCookie));assert(c);
 auto *md=dmaMemory(16384,false);assert(md);c->md=md;c->prepared=prepared;c->bytes=16384;
 c->kernelMap=new IOMemoryMap;maps.push_back(c->kernelMap);
 if(mapped)assert(gDart.mapMemory(md,c->bytes,&c->iova)==0);
 return c;
}
static unsigned allocatedPages() {
 unsigned n=0;for(unsigned i=0;i<gDart.pages;i++)n+=!!(gDart.bitmap[i/8]&(1u<<(i%8)));return n;
}
static void retained(DmaCookie *c) {
 assert(cookieAllocations.count(c));assert(c->md->completes==0&&c->md->releases==0);
 assert(c->kernelMap->unmaps==0&&c->kernelMap->releases==0);
}
static void normal() {
 auto*c=makeCookie();auto*md=c->md;auto*map=c->kernelMap;
 unsigned f=flushes;events.clear();sf_avd_os_dma_free(c);
 assert(flushes==f+1&&!gDart.poisoned&&allocatedPages()==0&&gDart.mappedBytes==0);
 assert(md->completes==1&&md->releases==1&&map->unmaps==1&&map->releases==1&&cookieFrees==1);
 assert(events==std::vector<std::string>({"invalidate","kernel-unmap","map-release","complete","descriptor-release"}));
}
static void normalKernelAllocation() {
 auto*c=makeCookie();auto*md=c->md;auto*unusedMap=c->kernelMap;c->kernelMap=nullptr;
 events.clear();sf_avd_os_dma_free(c);
 assert(!gDart.poisoned&&allocatedPages()==0&&gDart.mappedBytes==0&&cookieFrees==1);
 assert(md->completes==1&&md->releases==1&&!unusedMap->unmaps&&!unusedMap->releases);
 assert(events==std::vector<std::string>({"invalidate","complete","descriptor-release"}));
}
static void flushFailure() {
 auto*c=makeCookie();auto*md=c->md;auto*map=c->kernelMap;
 failInvalidate=true;sf_avd_os_dma_free(c);
 retained(c);assert(gDart.poisoned&&allocatedPages()==1&&gDart.mappedBytes==16384&&cookieFrees==0);
 assert(gDart.l2[0]&&gDart.l2[0][0]==0); // PTE cleared, reservation retained.
 assert(md->completes==0&&map->unmaps==0);
 unsigned f=flushes;sf_avd_os_dma_free(c);sf_avd_os_dma_free(c);
 retained(c);assert(flushes==f&&cookieFrees==0);
 assert(gQuarantined==c&&c->quarantineNext==nullptr&&c->quarantined);
 assert(gDart.quarantinedDescriptors==1&&gDart.quarantinedBytes==16384);
}
static void peerAfterPoison() {
 auto*a=makeCookie();auto*b=makeCookie();assert(allocatedPages()==2);
 failInvalidate=true;sf_avd_os_dma_free(a);unsigned f=flushes;
 sf_avd_os_dma_free(b);retained(a);retained(b);
 assert(gDart.poisoned&&allocatedPages()==2&&gDart.mappedBytes==32768&&flushes==f);
 assert(gQuarantined==b&&b->quarantineNext==a&&a->quarantineNext==nullptr);
 assert(gDart.quarantinedDescriptors==2&&gDart.quarantinedBytes==32768);
}
static void refuseAfterPoison() {
 auto*c=makeCookie();failInvalidate=true;sf_avd_os_dma_free(c);
 auto *md=dmaMemory(16384,false);uint64_t io=0;unsigned n=allocationCalls;
 assert(gDart.mapMemory(md,16384,&io)!=0&&io==0&&md->completes==0&&md->releases==0);
 assert(gDart.alloc(16384,&io)==nullptr&&allocationCalls==n&&io==0);
 assert(allocatedPages()==1&&gDart.mappedBytes==16384);
}
static void partialRollback(bool fail) {
 nextBadPage=16384;failInvalidate=fail;uint64_t io=0;
 auto *result=gDart.alloc(32768,&io);assert(!result);
 // Descriptor order: requested payload first, then any lazily allocated L2.
 auto *md=descriptors.front();assert(md->badPage==16384);
 if(fail) {
  assert(gDart.poisoned&&md->completes==0&&md->releases==0&&allocatedPages()==2&&io==SF_AVD_DART_BASE);
  assert(gDart.mappedBytes==32768&&gDart.quarantinedDescriptors==1&&gDart.quarantinedBytes==32768);
 } else {
  assert(!gDart.poisoned&&md->completes==1&&md->releases==1&&allocatedPages()==0&&gDart.mappedBytes==0&&io==0);
 }
}
static void partialImportRollback() {
 auto*c=(DmaCookie*)IOMalloc(sizeof(DmaCookie));assert(c);
 nextBadPage=16384;c->md=dmaMemory(32768,false);c->bytes=32768;c->prepared=true;
 c->kernelMap=new IOMemoryMap;maps.push_back(c->kernelMap);failInvalidate=true;
 assert(gDart.mapMemory(c->md,c->bytes,&c->iova)==ETIMEDOUT&&c->iova==SF_AVD_DART_BASE);
 assert(gDart.poisoned&&allocatedPages()==2&&gDart.mappedBytes==32768);
 sf_avd_os_dma_free(c);retained(c);
 assert(gQuarantined==c&&c->quarantineNext==nullptr&&c->quarantined);
 assert(gDart.quarantinedDescriptors==1&&gDart.quarantinedBytes==32768&&cookieFrees==0);
}
static void invalidUnmapRetains() {
 auto*c=makeCookie();c->iova++;
 unsigned f=flushes;sf_avd_os_dma_free(c);retained(c);
 assert(gDart.poisoned&&flushes==f&&allocatedPages()==1&&gDart.mappedBytes==16384);
 assert(gQuarantined==c&&c->quarantineNext==nullptr&&gDart.quarantinedDescriptors==1);
}
static void unmappedCleanup(bool prepared) {
 auto*c=makeCookie(false,prepared);auto*md=c->md;auto*map=c->kernelMap;unsigned f=flushes;
 sf_avd_os_dma_free(c);assert(flushes==f&&md->completes==unsigned(prepared)&&md->releases==1);
 assert(map->unmaps==1&&map->releases==1&&cookieFrees==1&&!gDart.poisoned);
}
static void cleanupFixtureOnly() {
 // Destroy host bookkeeping after assertions. This is not kernel reclamation:
 // deliberately quarantined production objects remain retained until reboot.
 for(auto*p:cookieAllocations)std::free(p);
 cookieAllocations.clear();
 for(auto*p:maps)delete p;
 for(auto*p:descriptors)delete p;
 std::free(gDart.bitmap);std::free(gDart.l1);
}
int main(int argc,char**argv) {
 assert(argc==2);setup();std::string test=argv[1];
 if(test=="normal")normal();
 else if(test=="normal-kernel-allocation")normalKernelAllocation();
 else if(test=="flush-failure-repeat")flushFailure();
 else if(test=="peer-after-poison")peerAfterPoison();
 else if(test=="refuse-after-poison")refuseAfterPoison();
 else if(test=="partial-rollback-ok")partialRollback(false);
 else if(test=="partial-rollback-failure")partialRollback(true);
 else if(test=="partial-import-rollback-failure")partialImportRollback();
 else if(test=="invalid-unmap-retains")invalidUnmapRetains();
 else if(test=="unmapped-prepared")unmappedCleanup(true);
 else if(test=="unmapped-unprepared")unmappedCleanup(false);
 else return 2;
 cleanupFixtureOnly();std::printf("DART_QUARANTINE logical=%s PASS\n",argv[1]);return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[5]
    kernel = repo/'integrations/siliconforest/m1-avd/kernel'
    paths = [kernel/'sf_avd_dart.cpp', kernel/'sf_avd_dart.h', kernel/'SFAppleAVD.cpp']
    dart, header, driver = (p.read_text() for p in paths)
    args.output.mkdir(parents=True, exist_ok=False)
    pieces = [STUBS]
    for text in [header, dart]:
        pieces += re.findall(r'(?m)^#define\s+(?!_SF_AVD_DART_H\b|DLOG\b)[^\n]+', text)
    pieces += ['#define DLOG(...) ((void)0)', structure(header, 'SFAvdDart')]
    for name in ['invalidate', 'dmaMemory', 'SFAvdDart::alloc', 'SFAvdDart::mapMemory', 'SFAvdDart::unmapMemory']:
        pieces.append(extract(dart, name))
    pieces += [structure(driver, 'DmaCookie'), 'static SFAvdDart gDart;']
    globals_ = re.findall(r'(?m)^static DmaCookie \*gQuarantined;[^\n]*', driver)
    if len(globals_) != 1:
        raise ValueError('Expected actual single quarantine list declaration')
    pieces += [globals_[0], extract(driver, 'quarantine_dma'), extract(driver, 'sf_avd_os_dma_free')]
    pieces.append(TESTS)
    source = '// SPDX-License-Identifier: GPL-2.0\n// Actual GPL kernel bodies with host-only stubs; generated by dart-quarantine-test.py.\n' + '\n\n'.join(pieces)
    source, barriers = re.subn(r'__asm__\s+volatile\("dsb sy"\s*:::\s*"memory"\);', '__asm__ volatile("" ::: "memory");', source)
    if not barriers:
        raise ValueError('Expected actual ARM barrier transformation')
    (args.output/'fixture.cpp').write_text(source)
    hashes = {str(p.relative_to(repo)):hashlib.file_digest(p.open('rb'),'sha256').hexdigest() for p in paths}
    (args.output/'source-hashes.json').write_text(json.dumps(dict(sources=hashes,generated_fixture_sha256=hashlib.sha256(source.encode()).hexdigest(),body_transform='ARM dsb sy -> host compiler fence',barriers=barriers,scope='Logical ownership/MMIO stubs; not hardware/DMA/task-pinning proof'),indent=2)+'\n')
    cc = os.environ.get('CXX', 'c++')
    command = [cc, '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-Wall', '-Wextra', str(args.output/'fixture.cpp'), '-o', str(args.output/'fixture')]
    (args.output/'compile-command.json').write_text(json.dumps(command,indent=2)+'\n')
    result = subprocess.run(command,capture_output=True,text=True)
    (args.output/'compile.log').write_text(result.stdout+result.stderr)
    result.check_returncode()
    rows=[]
    for name in ['normal','normal-kernel-allocation','flush-failure-repeat','peer-after-poison','refuse-after-poison','partial-rollback-ok','partial-rollback-failure','partial-import-rollback-failure','invalid-unmap-retains','unmapped-prepared','unmapped-unprepared']:
        result = subprocess.run([str(args.output/'fixture'),name],capture_output=True,text=True,timeout=15)
        rows.append(dict(name=name,returncode=result.returncode,stdout=result.stdout,stderr=result.stderr))
    (args.output/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
    for row in rows:
        print(row['stdout'],end='')
        if row['returncode']:
            print(row['stderr'])
    return int(any(row['returncode'] for row in rows))


if __name__=='__main__':
    raise SystemExit(main())
