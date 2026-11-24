/* Actual control_native.cpp, using explicit TEST SDK/API stand-ins only. */
#include "control_protection.h"
#include "native_pmap_bridge.h"
#include <IOKit/IOLocks.h>
#include <mach/vm_param.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef NDEBUG
#error "native control tests require assertions"
#endif
static test_pmap map{1};
extern "C" const pmap_t kernel_pmap=&map;
uint32_t test_page_shift=14;
uint64_t page_size=16384;
static int scenario,pins,configures,queries,getters;
static bpn_native_setup held;
static bpl_policy policy;
static bpl_registry admitted;
extern "C" int bpn_native_pin_empty(bpn_native_setup *out)
{
    pins++;assert(pins==1);if(scenario==1)return -1;
    held={0x100008,7,{0x100008,0x5000},0x203ff8};*out=held;
    if(scenario==8)policy.platform_domain++;
    return 0;
}
extern "C" lck_mtx_t *IOLockGetMachLock(IOLock *opaque)
{
    getters++;assert(pins==1 && reinterpret_cast<uintptr_t>(opaque)==held.opaque_mutex_identity);
    return reinterpret_cast<lck_mtx_t *>(held.opaque_mutex_identity+(scenario==2?8:0));
}
extern "C" ppnum_t pmap_find_phys(pmap_t m,addr64_t va)
{
    assert(m==kernel_pmap && pins==1 && getters && !configures && !(va&0x3fff));queries++;assert(queries<=16);
    if(scenario==3)return 0;if(scenario==4)return 0x400000;
    ppnum_t ppn=0x200000+static_cast<uint32_t>((va>>14)&31u)*2;
    if(scenario==5 && queries==5)ppn++;
    if(scenario==6)test_page_shift=12;
    return ppn;
}
extern "C" int bpn_native_configure(const bpn_native_setup *s,const bpl_policy *p,const bpn_protection *protection)
{
    configures++;assert(configures==1 && queries==8 && getters==3);
    assert(s->identity==held.identity && s->generation==held.generation && s->opaque_mutex_identity==held.opaque_mutex_identity);
    assert(protection->complete==1 && protection->provenance==held.identity && protection->cpu_count==2 && protection->pa_count==2);
    assert(protection->cpu[1].base==0x200000 && protection->cpu[1].bytes==0x8000);
    assert(p->reserved_count==3 && p->reserved[0].base==UINT64_C(0x800000000) && p->reserved[0].bytes==0x4000);
    assert(p->reserved[1].base==UINT64_C(0x800008000) && p->reserved[2].base==UINT64_C(0x800f00000));
    if(scenario==7)return -1;
    assert(bpl_init(&admitted,p)==0);return 0;
}
int main(int argc,char **argv)
{
    assert(argc==2);scenario=std::atoi(argv[1]);
    auto *w=static_cast<bcp_workspace *>(std::calloc(1,sizeof(bcp_workspace)));
    auto *out=static_cast<bcp_receipt *>(std::malloc(sizeof(bcp_receipt)));
    auto *saved=static_cast<bcp_receipt *>(std::malloc(sizeof(bcp_receipt)));assert(w && out && saved);
    std::memset(out,0xa5,sizeof *out);*saved=*out;
    policy.platform_domain=1;policy.dart_domain=2;policy.irq_domain=3;policy.reserved_count=1;
    policy.ram[0]={UINT64_C(0x800000000),0x1000000};policy.reserved[0]={UINT64_C(0x800f00000),16384};
    assert(bcp_native_derive_and_configure(&policy,w,out)==BCP_POLICY && !pins && !queries && !configures);
    policy.ram_count=1;
    int result=bcp_native_derive_and_configure(&policy,w,out);
    if(!scenario){assert(result==0 && pins==1 && configures==1 && out->configured==1);*saved=*out;}
    else {assert(result!=0 && pins==1);assert(!std::memcmp(out,saved,sizeof *out));if(scenario!=7)assert(!configures);}
    int oldq=queries,oldc=configures;
    assert(bcp_native_derive_and_configure(&policy,w,out)==BCP_NATIVE && pins==1 && queries==oldq && configures==oldc);
    assert(!std::memcmp(out,saved,sizeof *out));
    std::free(w);std::free(out);std::free(saved);
    std::puts("ACTUAL CONTROL ADAPTER WITH TEST SDK PASS; no native translation proof");return 0;
}
