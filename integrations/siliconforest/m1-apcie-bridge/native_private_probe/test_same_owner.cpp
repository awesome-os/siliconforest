/* HOST ONLY actual frozen native facade via original TEST-SDK stand-ins. */
#define main frozen_combined_main_not_invoked
#define pmap_find_phys frozen_test_pmap_find_phys
#include "../owned_control/test_combined.cpp"
#undef pmap_find_phys
#undef main
#include "probe.h"
extern "C" void npp_test_identity(nps_expected *,int);
static int mode,retire_calls;
extern "C" ppnum_t pmap_find_phys(pmap_t p,addr64_t v)
{return frozen_test_pmap_find_phys(p,v)+(mode==1?0u:0x100u);}
static int labels(void *,nps_expected *e) {npp_test_identity(e,mode==2);return 0;}
static int configure(void *,const npp_expected *e,npp_workspace *w,npp_result *r) {return npp_native_configure_once(e,w,r);}
static int acquire(void *,const bno_request *q,uint64_t i,uint32_t p,bno_token *t) {return bno_native_acquire(q,i,p,t);}
static int zero(void *,const bno_token *t) {return bno_native_zero(t);}
static int copy(void *,const bno_token *t,uint64_t o,const void *p,size_t n) {return bno_native_copy(t,o,p,n);}
static int inspect(void *,uint32_t slot,bno_snapshot *s) {return bno_native_inspect(slot,s);}
static int retire(void *,const bno_token *t) {retire_calls++;return bno_native_retire(t);}
int main(int argc,char **argv)
{
    assert(argc==2);mode=std::atoi(argv[1]);assert(mode>=0 && mode<=4);allow_descriptors=1;
    if(mode==3){sdk_fail_action=2;sdk_fail_role=1;}
    if(mode==4){sdk_fail_action=6;sdk_fail_role=1;sdk_after=1;}
    auto *w=static_cast<bcp_workspace *>(std::calloc(1,sizeof(bcp_workspace)));
    auto *r=static_cast<bcp_record *>(std::malloc(sizeof(bcp_record)));assert(w && r);bcp_init(r,1);
    const bcp_ops ops={nullptr,labels,configure,acquire,zero,copy,inspect,retire};bcp_run(&ops,w,r);
    if(mode==0) {
        assert(bcp_validate(r->bytes,BCP_BYTES,w->expected.identity.nonce)==0 && retire_calls==1);
        assert(sdk_allocations==3 && sdk_prepares==3 && sdk_completes==3 && sdk_resets==3 && !sdk_live);
        assert(bcp_word(r,BCP_PRE_AT+24)==3 && bcp_word(r,BCP_POST_AT+24)==0 && bcp_word(r,BCP_POST_AT+28)==3);
        assert(!std::strcmp(w->policy.receipt.provider_freeze,"0ee2101c9cda5cf282c3db48b7640cef98146c3b04724e3fe59138aaad772434"));
    } else {
        assert(bcp_validate(r->bytes,BCP_BYTES,w->expected.identity.nonce)==1 && bcp_word(r,BCP_RESULT_AT)==BCP_FAILED);
        assert(retire_calls==(mode==4));
        if(mode==1 || mode==2)assert(!sdk_allocations && !sdk_prepares);
        if(mode==3)assert(sdk_live==3 && !sdk_completes && !sdk_resets);
        if(mode==4)assert(sdk_live==3 && sdk_completes==2 && !sdk_resets);
    }
    assert(!bcp_word(r,BCP_READBACK_AT) && !bcp_word(r,BCP_EFFECT_AT));
    std::free(w);std::free(r);std::puts("SAME-OWNER PRIVATE PREFIX / HOST TEST SDK ONLY PASS; no readback/effects");return 0;
}
