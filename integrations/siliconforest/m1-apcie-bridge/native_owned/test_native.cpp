/* Execute the actual adapter against explicitly fake API classes, never XNU. */
#include "native_owned.h"
#include "owned.h"
#include "test_sdk/sdk.hpp"
#include <cstdio>
#ifdef NDEBUG
#error "native adapter tests require assertions"
#endif
uint64_t page_size=16384;
task_t kernel_task=reinterpret_cast<void *>(1);
int sdk_lock_depth,sdk_allocations,sdk_prepares,sdk_completes,sdk_resets,sdk_live;
int sdk_fail_action,sdk_fail_role,sdk_after;
static unsigned queries;
#ifdef BNO_TEST_DISPATCH
static unsigned effects;
static bno_token test_token;
#endif
void sdk_observe(int action)
{
    if(action==3)queries++;
    bno_snapshot s{};assert(bno_native_inspect(0,&s)==0);assert(s.busy && s.call_pending);
    if(action==6)assert(!s.core_retired);
    if(action==7)assert(s.core_retired);
    bno_request q{0x101,1,2};bno_token other{};
    assert(bno_native_acquire(&q,0x200000,BPL_READ,&other)==BNO_BUSY);
}
#ifdef BNO_TEST_DISPATCH
int bno_test_effect_observe(const bno_effect_call *c)
{
    assert(!sdk_lock_depth && sdk_live==3 && sdk_prepares==3 && !sdk_completes && !sdk_resets);
    bno_snapshot s{};assert(!bno_native_inspect(c->slot,&s));
    assert(s.busy && s.call_pending && s.effect_pending==c->ticket.sequence &&
        s.effect_count==effects+1 && s.phase==BNO_EFFECT_PENDING && s.ever_resources);
    assert(c->ticket.lease.generation==c->generation && c->ticket.lease.slot==c->slot);
    bno_token token=test_token;assert(token.generation==c->generation && token.slot==c->slot);
    assert(bno_native_retire(&token)==BNO_BUSY && bno_native_zero(&token)==BNO_BUSY);
    bno_effect_result out{};assert(bno_native_run(&token,&c->request,&out)==BNO_BUSY);
    for(unsigned i=0;i<3;i++)assert(c->identities[i] && c->views[i].cpu && c->views[i].span==16384);
    effects++;return sdk_fail_action==8?sdk_after:0;
}
#endif
int main(int argc,char **argv)
{
    assert(argc==4);sdk_fail_action=std::atoi(argv[1]);sdk_fail_role=std::atoi(argv[2]);sdk_after=std::atoi(argv[3]);
    bno_request q{0x100,0,1};bno_token token{};
    assert(bno_native_acquire(&q,0x100000,BPL_READ,&token)==BNO_ARGUMENT);
    if(sdk_fail_action==99) {
        page_size=4096;bno_native_setup refused{};
        assert(bno_native_pin_empty(&refused)==BNO_PAGE && !sdk_allocations);
        std::puts("ACTUAL ADAPTER WITH TEST SDK PASS; page-size refusal");return 0;
    }
    bno_native_setup setup{};assert(bno_native_pin_empty(&setup)==0);
    assert(setup.identity && setup.generation && setup.typed_anchor_cpu.base && setup.typed_anchor_cpu.bytes && setup.opaque_mutex_identity);
    assert(bno_native_pin_empty(&setup)==BNO_ARGUMENT);
    bpl_policy p{};p.platform_domain=1;p.dart_domain=2;p.irq_domain=3;p.ram_count=1;
    p.ram[0]={UINT64_C(0x800000000),0x1000000};
    bno_protection protect{};
    assert(bno_native_configure(&setup,&p,&protect)==BNO_VALIDATION);
    assert(!sdk_allocations && bno_native_acquire(&q,0x100000,BPL_READ,&token)==BNO_ARGUMENT);
    protect.provenance=123;protect.complete=protect.pa_count=protect.cpu_count=1;
    protect.pa[0]={UINT64_C(0x800f00000),16384};protect.cpu[0]=setup.typed_anchor_cpu;
    bno_native_setup wrong=setup;wrong.identity++;
    assert(bno_native_configure(&wrong,&p,&protect)==BNO_ARGUMENT && !sdk_allocations);
    assert(bno_native_configure(&setup,&p,&protect)==0);
    assert(bno_native_configure(&setup,&p,&protect)==BNO_ARGUMENT);
    int result=bno_native_acquire(&q,0x100000,BPL_READ|BPL_WRITE,&token);
    if(sdk_fail_action==1) {
        assert(result==BNO_REFUSED && sdk_live==0 && sdk_resets==sdk_fail_role && !sdk_prepares && !sdk_completes);
    } else if(sdk_fail_action==2) {
        assert(result==BNO_RETAINED && sdk_live==3 && !sdk_completes && !sdk_resets);
    } else {
        assert(result==0 && sdk_live==3 && sdk_prepares==3);
        assert(bno_native_zero(&token)==0);uint8_t payload[4]={1,2,3,4};assert(bno_native_copy(&token,0,payload,4)==0);
        bno_effect_request request{BNO_OP_SUBMIT,0,8,16,{0x5a,0}};
        bno_effect_result output{123,456,789};
#ifndef BNO_TEST_DISPATCH
        unsigned before=queries;
        assert(bno_native_run(&token,&request,&output)==BNO_UNAVAILABLE);
        assert(output.sequence==123 && output.operation==456 && output.ticket_sequence==789 && queries==before);
        bno_snapshot snap{};assert(!bno_native_inspect(0,&snap) && !snap.effect_count && !snap.effect_pending);
        bno_token stale=token;stale.generation++;
        assert(bno_native_run(&stale,&request,&output)==BNO_STALE && queries==before);
        request.reserved=1;assert(bno_native_run(&token,&request,&output)==BNO_ARGUMENT && queries==before);request.reserved=0;
#else
        test_token=token;int run=bno_native_run(&token,&request,&output);
        if(sdk_fail_action==8) {
            assert(run==BNO_RETAINED && output.sequence==123 && sdk_live==3 && !sdk_completes && !sdk_resets);
            bno_snapshot snap{};assert(!bno_native_inspect(0,&snap) && snap.halted && snap.phase==BNO_QUARANTINED && snap.effect_count==1);
            token={};assert(sdk_live==3);std::puts("ACTUAL ADAPTER WITH TEST SDK PASS; effect fault retained");return 0;
        }
        assert(run==0 && effects==1 && output.ticket_sequence==1);
        for(unsigned i=1;i<32;i++)assert(!bno_native_run(&token,&request,&output));
        unsigned before=queries;assert(bno_native_run(&token,&request,&output)==BNO_LIMIT && queries==before && effects==32);
        assert(bno_native_retire(&token)==BNO_RETAINED && bno_native_zero(&token)==BNO_RETAINED &&
            bno_native_copy(&token,0,payload,4)==BNO_RETAINED && sdk_live==3 && !sdk_completes && !sdk_resets);
        token={};assert(sdk_live==3);std::puts("ACTUAL ADAPTER WITH TEST SDK PASS; 32 retained effects");return 0;
#endif
        result=bno_native_retire(&token);
        if(sdk_fail_action==6)assert(result==BNO_RETAINED && sdk_live==3 && !sdk_resets && sdk_completes==3-sdk_fail_role);
        else assert(result==0 && !sdk_live && sdk_resets==3 && sdk_completes==3);
    }
    if(sdk_live) {token={};assert(sdk_live==3);bno_snapshot s{};assert(bno_native_inspect(0,&s)==0 && s.phase==BNO_QUARANTINED);}
    std::puts("ACTUAL ADAPTER WITH TEST SDK PASS; no native runtime");return 0;
}
