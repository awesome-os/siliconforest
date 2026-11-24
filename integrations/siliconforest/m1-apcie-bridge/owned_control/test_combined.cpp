/* One HOST binary: actual new control + frozen owner + frozen canonical core.
 * All kernel APIs below are original TEST stand-ins, never native execution. */
#include "owned_control.h"
#include "owned_pmap_bridge.h"
#include "../native_owned/native_owned.h"
#include "combined_sdk/sdk.hpp"
#include <cstdio>
#ifdef NDEBUG
#error "combined tests require assertions"
#endif
static test_pmap map{1};
extern "C" const pmap_t kernel_pmap=&map;
uint32_t test_page_shift=14;
uint64_t page_size=16384;
task_t kernel_task=reinterpret_cast<void *>(1);
int sdk_lock_depth,sdk_allocations,sdk_prepares,sdk_completes,sdk_resets,sdk_live;
int sdk_fail_action,sdk_fail_role,sdk_after;
uint64_t test_descriptor_pa=UINT64_C(0x800800000),test_descriptor_cpu_alias;
static int scenario,control_allocs,mutex_allocs,queries,getters,allow_descriptors,descriptor_queries;
static void *control_memory;
static size_t control_bytes;
static IOLock *control_mutex;
static uint64_t seen_pages[8];
static unsigned seen_count;
static bpl_policy policy;
void *test_control_allocate(size_t n)
{
    assert(!sdk_lock_depth && !control_allocs++ && n && n<16384);
    if(scenario==1)return nullptr;
    void *raw=nullptr;assert(!posix_memalign(&raw,16384,3*16384));std::memset(raw,0,3*16384);
    // Deliberately cross a 16KiB boundary; owner supplies its actual sizeof.
    control_memory=static_cast<uint8_t*>(raw)+16384-8;control_bytes=n;return control_memory;
}
IOLock *test_mutex_allocate()
{
    assert(control_allocs==1 && control_memory && !mutex_allocs++);
    if(scenario==2)return nullptr;
    control_mutex=new IOLock;return control_mutex;
}
extern "C" lck_mtx_t *IOLockGetMachLock(IOLock *lock)
{
    assert(lock==control_mutex && !sdk_allocations);getters++;
    if(scenario==3)return reinterpret_cast<lck_mtx_t *>(reinterpret_cast<uintptr_t>(lock)+8);
    assert(reinterpret_cast<uintptr_t>(&lock->typed)==reinterpret_cast<uintptr_t>(lock));return &lock->typed;
}
extern "C" ppnum_t pmap_find_phys(pmap_t m,addr64_t va)
{
    assert(m==kernel_pmap && getters && control_memory && control_mutex && !sdk_allocations && !(va&16383));
    unsigned index=0;for(;index<seen_count;index++)if(seen_pages[index]==va)break;
    if(index==seen_count){assert(seen_count<8);seen_pages[seen_count++]=va;}
    queries++;assert(queries<=16);
    if(scenario==4)return 0;
    if(scenario==5)return 0x400000;
    if(scenario==7)return 0x180000;
    if(scenario==8)return static_cast<ppnum_t>(UINT64_C(0x800f00000)>>14);
    if(scenario==9)test_page_shift=12;
    if(scenario==10)policy.irq_domain++;
    ppnum_t result=0x200000+index*2;
    if(scenario==6 && queries>static_cast<int>(seen_count))result++;
    return result;
}
void sdk_observe(int action)
{
    if(action==3)descriptor_queries++;
    assert(allow_descriptors && !sdk_lock_depth);
    bno_snapshot snapshot{};assert(!bno_native_inspect(0,&snapshot));
    assert(snapshot.busy && snapshot.call_pending && !snapshot.effect_count && !snapshot.ever_resources);
    if(action==6)assert(!snapshot.core_retired);
    if(action==7)assert(snapshot.core_retired);
    bno_request q{0x101,1,2};bno_token t{};assert(bno_native_acquire(&q,0x200000,1,&t)==BNO_BUSY);
}
static bool contains(const bpl_range *ranges,unsigned count,uint64_t base,uint64_t bytes)
{
    for(unsigned i=0;i<count;i++)if(base>=ranges[i].base && bytes<=ranges[i].bytes &&
        base-ranges[i].base<=ranges[i].bytes-bytes)return true;
    return false;
}
int main(int argc,char **argv)
{
    assert(argc==2);scenario=std::atoi(argv[1]);assert(scenario>=0 && scenario<=16);
    auto *w=static_cast<boc_workspace*>(std::calloc(1,sizeof(boc_workspace)));
    auto *out=static_cast<boc_receipt*>(std::malloc(sizeof(boc_receipt)));
    auto *saved=static_cast<boc_receipt*>(std::malloc(sizeof(boc_receipt)));assert(w && out && saved);
    std::memset(out,0xa5,sizeof *out);*saved=*out;
    policy.platform_domain=1;policy.dart_domain=2;policy.irq_domain=3;
    policy.ram[0]={UINT64_C(0x800000000),0x1000000};policy.reserved_count=1;
    policy.reserved[0]={UINT64_C(0x800f00000),16384};
    assert(boc_native_derive_and_configure(&policy,w,out)==BOC_POLICY && !control_allocs && !sdk_allocations);
    policy.ram_count=1;
    if(scenario==11){policy.ram_count=2;policy.ram[0].bytes=0x800000;policy.ram[1]={UINT64_C(0x800800000),0x800000};}
    assert(boc_native_derive_and_configure(&policy,w,reinterpret_cast<boc_receipt*>(w))==BOC_ARGUMENT && !control_allocs);
    if(scenario==16){bno_native_setup foreign{};assert(!bno_native_pin_empty(&foreign));}
    int result=boc_native_derive_and_configure(&policy,w,out);
    if((scenario>=1 && scenario<=10) || scenario==16) {
        assert(result!=0 && !std::memcmp(out,saved,sizeof *out) && !sdk_allocations && !sdk_prepares);
        bno_request q{0x100,0,1};bno_token t{};assert(bno_native_acquire(&q,0x100000,3,&t)==BNO_ARGUMENT);
    } else {
        assert(!result && control_allocs==1 && mutex_allocs==1 && out->configured==1);
        assert(!std::strcmp(out->provider_freeze,"0ee2101c9cda5cf282c3db48b7640cef98146c3b04724e3fe59138aaad772434"));
        assert(out->setup.identity==reinterpret_cast<uintptr_t>(control_memory) &&
            out->setup.typed_anchor_cpu.base==out->setup.identity && out->setup.typed_anchor_cpu.bytes==control_bytes);
        assert(out->setup.opaque_mutex_identity==reinterpret_cast<uintptr_t>(control_mutex) &&
            out->mutex_span.bytes==16 && out->source.mutex_align==8);
        assert(out->base_policy.ram_count==policy.ram_count && out->final_policy.ram_count==policy.ram_count);
        for(unsigned i=0;i<policy.ram_count;i++)assert(out->final_policy.ram[i].base==policy.ram[i].base && out->final_policy.ram[i].bytes==policy.ram[i].bytes);
        assert(out->page_count==seen_count && seen_count>=3 && queries==static_cast<int>(seen_count*2) && getters==3);
        assert(contains(out->protection.cpu,out->protection.cpu_count,out->setup.identity,control_bytes));
        assert(contains(out->protection.cpu,out->protection.cpu_count,out->mutex_span.base,16));
        for(unsigned i=0;i<seen_count;i++) {
            assert(out->cpu_pages[i]==seen_pages[i] && out->ppnums[i]==0x200000+i*2);
            assert(out->pa_pages[i]==(static_cast<uint64_t>(out->ppnums[i])<<14));
            assert(contains(out->final_policy.reserved,out->final_policy.reserved_count,out->pa_pages[i],16384));
            if(i+1<seen_count)assert(!contains(out->protection.pa,out->protection.pa_count,out->pa_pages[i]+16384,16384));
        }
        // Detached old provenance is rejected, even when both copies agree.
        const char old[]="79b153f2de3c0899a1a59f42b2b2d24dd468588723ca9f75109d53d90269656b";
        std::memcpy(w->expected.provider_freeze,old,sizeof old);std::memcpy(w->candidate.provider_freeze,old,sizeof old);
        w->candidate.configured=0;assert(boc_check_workspace(w)==BOC_BINDING);
        // Not a configure authentication API: high-level API accepts no receipt.
        if(scenario==12)test_descriptor_pa=out->pa_pages[0];
        if(scenario==13)test_descriptor_cpu_alias=(out->setup.identity&~UINT64_C(16383))+16384;
        if(scenario==14){sdk_fail_action=2;sdk_fail_role=1;}
        if(scenario==15){sdk_fail_action=6;sdk_fail_role=1;sdk_after=1;}
        allow_descriptors=1;bno_request request{0x100,0,1};bno_token token{};
        result=bno_native_acquire(&request,0x100000,3,&token);
        if(scenario==12 || scenario==13) {
            assert(result==BNO_VALIDATION && !sdk_live && sdk_completes==3 && sdk_resets==3);
        } else if(scenario==14) {
            assert(result==BNO_RETAINED && sdk_live==3 && !sdk_completes && !sdk_resets);
        } else {
            assert(!result && sdk_live==3 && sdk_prepares==3);
            assert(!bno_native_zero(&token));uint8_t bytes[4]={1,2,3,4};assert(!bno_native_copy(&token,0,bytes,4));
            bno_effect_request effect{BNO_OP_SUBMIT,0,0,4,{0,0}};bno_effect_result value{123,456,789};
            int before_queries=queries,before_descriptor_queries=descriptor_queries;
            assert(bno_native_run(&token,&effect,&value)==BNO_UNAVAILABLE && queries==before_queries && descriptor_queries==before_descriptor_queries);
            assert(value.sequence==123 && value.operation==456 && value.ticket_sequence==789);
            bno_snapshot snapshot{};assert(!bno_native_inspect(0,&snapshot) && !snapshot.effect_count && !snapshot.ever_resources);
            result=bno_native_retire(&token);
            if(scenario==15)assert(result==BNO_RETAINED && sdk_live==3 && !sdk_resets && sdk_completes==2);
            else assert(!result && !sdk_live && sdk_completes==3 && sdk_resets==3);
        }
        *saved=*out;
    }
    int old_queries=queries,old_controls=control_allocs,old_buffers=sdk_allocations;
    assert(boc_native_derive_and_configure(&policy,w,out)==BOC_NATIVE && queries==old_queries &&
        control_allocs==old_controls && sdk_allocations==old_buffers && !std::memcmp(out,saved,sizeof *out));
    std::free(w);std::free(out);std::free(saved);
    std::puts("COMBINED OWNED CONTROL HOST PASS; actual sources, TEST APIs only");return 0;
}
