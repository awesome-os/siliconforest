/* SPDX-License-Identifier: MIT */
/* Actual platform bodies; host API instruments lifetime, not an XNU/physical-DMA test. */
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <initializer_list>
#include "sf_avd_core.h"
using vm_offset_t = uintptr_t;
using task_t = void *;
using pid_t = int;
using user_addr_t = uint64_t;
static unsigned freed, unmapped, released_maps, completed, released_md, cmos;
static uintptr_t cmo_addresses[16];
static int result, dart_result, unmap_result;
struct IOMemoryDescriptor {
    size_t length;
    size_t getLength(){return length;}
    void complete(){completed++;}
    void release(){released_md++;}
};
struct IOMemoryMap {
    uintptr_t address; size_t length;
    uintptr_t getAddress(){return address;}
    size_t getLength(){return length;}
    void unmap(){unmapped++;}
    void release(){released_maps++;}
};
static struct {
    bool poisoned;
    uint32_t quarantinedDescriptors;
    uint64_t quarantinedBytes;
    int unmapMemory(uint64_t,size_t){return unmap_result;}
    int check(const char *){return dart_result;}
} gDart;
static void CleanPoC_DcacheRegion_Force(vm_offset_t a,size_t n){assert(n==16384 && cmos<16);cmo_addresses[cmos++]=a;}
static void IOFree(void *,size_t){freed++;}
static int proc_selfpid(){return 42;}
static task_t current_task(){return (void *)0x1234;}
static int copy_ctrl(const sfavd_ctrl *,uint8_t **){return EINVAL;}
static int copyin(user_addr_t,void *,size_t){return 0;}
static void set_state(const char *){}
extern "C" int sf_avd_session_set_ctrl(sf_avd_session *,uint32_t,const void *,size_t){return 0;}
extern "C" void *sf_avd_session_coded(sf_avd_session *,size_t){return (void *)0x4321;}
extern "C" int sf_avd_session_decode(sf_avd_session *,size_t,unsigned,uint64_t,uint32_t,uint32_t *){return result;}
extern "C" uint64_t sf_avd_os_now_us(){return 1;}
#include SF_NATIVE_UNIT_HEADER
static IOMemoryDescriptor md;
static IOMemoryMap map;
static DmaCookie cookie;
static void reset(){
    freed=unmapped=released_maps=completed=released_md=cmos=0;
    result=dart_result=unmap_result=0;gDart={};gQuarantined=nullptr;
    md.length=32768;map.address=0x1004000;map.length=32768;
    cookie={};cookie.md=&md;cookie.kernelMap=&map;cookie.prepared=true;cookie.bytes=16385;cookie.iova=0x4000000;
    context={};context.owner=42;context.ownerTask=current_task();context.imported=true;
}
int main(){
    reset();assert(!sf_avd_os_dma_sync(&cookie,SF_AVD_DMA_INITIAL));
    assert(cmos==2 && cmo_addresses[0]==0x1004000 && cmo_addresses[1]==0x1008000);
    /* Header at 0x1000000 is untouched; only descriptor's rounded DATA extent. */
    reset();md.length=16384;assert(sf_avd_os_dma_sync(&cookie,SF_AVD_DMA_BEFORE_DEVICE)==-EIO && !cmos);
    reset();map.length=16384;assert(sf_avd_os_dma_sync(&cookie,SF_AVD_DMA_BEFORE_DEVICE)==-EIO && !cmos);
    reset();map.address=UINTPTR_MAX-16384;assert(sf_avd_os_dma_sync(&cookie,SF_AVD_DMA_BEFORE_DEVICE)==-EIO && !cmos);
    reset();cookie.prepared=false;assert(sf_avd_os_dma_sync(&cookie,SF_AVD_DMA_BEFORE_DEVICE)==-EIO && !cmos);
    reset();gDart.poisoned=true;assert(sf_avd_os_dma_sync(&cookie,SF_AVD_DMA_BEFORE_DEVICE)==-EIO && !cmos);
    reset();cookie.kernelMap=nullptr;assert(!sf_avd_os_dma_sync(&cookie,SF_AVD_DMA_BEFORE_DEVICE) && !cmos);
    reset();sf_avd_os_dma_free(&cookie);assert(unmapped==1 && released_maps==1 && completed==1 && released_md==1 && freed==1);
    reset();unmap_result=EIO;sf_avd_os_dma_free(&cookie);
    assert(gDart.poisoned && gQuarantined==&cookie && !unmapped && !released_maps && !completed && !released_md && !freed);
    assert(gDart.quarantinedBytes==32768 && gDart.quarantinedDescriptors==1);
    sf_avd_os_dma_free(&cookie);assert(gDart.quarantinedDescriptors==1);
    for(int failure : { -EIO, -ETIMEDOUT, -EINVAL }) {
        reset();result=failure;sfavd_decode request={};request.size=1;request.data=1;
        assert(do_decode(&request)==-failure && gDart.poisoned);
        sf_avd_os_dma_free(&cookie);assert(gQuarantined==&cookie && !completed && !freed);
    }
    reset();dart_result=1;sfavd_decode request={};request.size=1;request.data=1;
    assert(do_decode(&request)==EIO && gDart.poisoned);
    reset();request={};request.size=1;request.data=1;assert(do_decode(&request)==0 && !gDart.poisoned);
    reset();context.imported=false;result=-EIO;request={};request.size=1;request.data=1;
    assert(do_decode(&request)==EIO && !gDart.poisoned);
    puts("AVD_NATIVE_OWNERSHIP actual-source DATA bounds/header exclusion, page chunks, legacy path, teardown, DART/decode failure quarantine PASS (host only)");
}
