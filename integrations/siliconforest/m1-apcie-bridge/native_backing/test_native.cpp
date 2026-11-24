/* Execute the actual adapter against explicitly fake API classes, never XNU. */
#include "native_iokit.h"
#include "test_sdk/sdk.hpp"
#include <cstdio>
#ifdef NDEBUG
#error "native adapter tests require assertions"
#endif
uint64_t page_size=16384;
task_t kernel_task=reinterpret_cast<void *>(1);
int sdk_lock_depth,sdk_allocations,sdk_prepares,sdk_completes,sdk_resets,sdk_live;
int sdk_fail_action,sdk_fail_role,sdk_after;
void sdk_observe(int action)
{
    bpn_snapshot s{};assert(bpn_native_inspect(0,&s)==0);assert(s.busy && s.call_pending);
    if(action==6)assert(!s.core_retired);
    if(action==7)assert(s.core_retired);
    bpl_request q{0x101,1,2};bpn_token other{};
    assert(bpn_native_acquire(&q,0x200000,BPL_READ,&other)==BPN_BUSY);
}
int main(int argc,char **argv)
{
    assert(argc==4);sdk_fail_action=std::atoi(argv[1]);sdk_fail_role=std::atoi(argv[2]);sdk_after=std::atoi(argv[3]);
    bpl_request q{0x100,0,1};bpn_token token{};
    assert(bpn_native_acquire(&q,0x100000,BPL_READ,&token)==BPN_ARGUMENT);
    if(sdk_fail_action==99) {
        page_size=4096;bpn_native_setup refused{};
        assert(bpn_native_pin_empty(&refused)==BPN_PAGE && !sdk_allocations);
        std::puts("ACTUAL ADAPTER WITH TEST SDK PASS; page-size refusal");return 0;
    }
    bpn_native_setup setup{};assert(bpn_native_pin_empty(&setup)==0);
    assert(setup.identity && setup.generation && setup.typed_anchor_cpu.base && setup.typed_anchor_cpu.bytes && setup.opaque_mutex_identity);
    assert(bpn_native_pin_empty(&setup)==BPN_ARGUMENT);
    bpl_policy p{};p.platform_domain=1;p.dart_domain=2;p.irq_domain=3;p.ram_count=1;
    p.ram[0]={UINT64_C(0x800000000),0x1000000};
    bpn_protection protect{};
    assert(bpn_native_configure(&setup,&p,&protect)==BPN_VALIDATION);
    assert(!sdk_allocations && bpn_native_acquire(&q,0x100000,BPL_READ,&token)==BPN_ARGUMENT);
    protect.provenance=123;protect.complete=protect.pa_count=protect.cpu_count=1;
    protect.pa[0]={UINT64_C(0x800f00000),16384};protect.cpu[0]=setup.typed_anchor_cpu;
    bpn_native_setup wrong=setup;wrong.identity++;
    assert(bpn_native_configure(&wrong,&p,&protect)==BPN_ARGUMENT && !sdk_allocations);
    assert(bpn_native_configure(&setup,&p,&protect)==0);
    assert(bpn_native_configure(&setup,&p,&protect)==BPN_ARGUMENT);
    int result=bpn_native_acquire(&q,0x100000,BPL_READ,&token);
    if(sdk_fail_action==1) {
        assert(result==BPN_REFUSED && sdk_live==0 && sdk_resets==sdk_fail_role && !sdk_prepares && !sdk_completes);
    } else if(sdk_fail_action==2) {
        assert(result==BPN_RETAINED && sdk_live==3 && !sdk_completes && !sdk_resets);
    } else {
        assert(result==0 && sdk_live==3 && sdk_prepares==3);
        assert(bpn_native_zero(&token)==0);uint8_t payload[4]={1,2,3,4};assert(bpn_native_copy(&token,0,payload,4)==0);
        result=bpn_native_retire(&token);
        if(sdk_fail_action==6)assert(result==BPN_RETAINED && sdk_live==3 && !sdk_resets && sdk_completes==3-sdk_fail_role);
        else assert(result==0 && !sdk_live && sdk_resets==3 && sdk_completes==3);
    }
    if(sdk_live) {token={};assert(sdk_live==3);bpn_snapshot s{};assert(bpn_native_inspect(0,&s)==0 && s.phase==BPN_QUARANTINED);}
    std::puts("ACTUAL ADAPTER WITH TEST SDK PASS; no native runtime");return 0;
}
