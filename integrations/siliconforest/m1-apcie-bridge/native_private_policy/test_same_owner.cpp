/* HOST ONLY: actual frozen reader/control/owner/core, explicit TEST SDK calls. */
#define main frozen_combined_main_not_invoked
#define pmap_find_phys frozen_test_pmap_find_phys
#include "../owned_control/test_combined.cpp"
#undef pmap_find_phys
#undef main
#include "private_policy.h"
extern "C" void npp_test_identity(struct nps_expected *,int);
static int mode;
extern "C" ppnum_t pmap_find_phys(pmap_t p,addr64_t v)
{
    /* A synthetic mapping, never an observed native physical address. */
    return frozen_test_pmap_find_phys(p,v)+(mode==1?0u:0x100u);
}
int main(int argc,char **argv)
{
    assert(argc==2);mode=std::atoi(argv[1]);assert(mode>=0 && mode<=2);
    auto *w=static_cast<npp_workspace*>(std::calloc(1,sizeof(npp_workspace)));
    assert(w);npp_expected e{};npp_test_identity(&e.identity,mode==2);
    e.platform_domain=1;e.dart_domain=2;e.irq_domain=3;e.profile=NPP_VM_PRIVATE_PROFILE;
    npp_result r,before;std::memset(&r,0xa5,sizeof r);before=r;
    int rc=npp_native_configure_once(&e,w,&r);
    if(mode==0) {
        assert(rc==NPP_OK && r.version==1 && r.control_pages>=3 && r.queries==r.control_pages*2);
        assert(!std::strcmp(w->receipt.provider_freeze,"0ee2101c9cda5cf282c3db48b7640cef98146c3b04724e3fe59138aaad772434"));
        assert(w->receipt.setup.identity==reinterpret_cast<uintptr_t>(control_memory));
        assert(control_allocs==1 && mutex_allocs==1 && !sdk_allocations && !sdk_prepares && !sdk_live);
        bno_snapshot s{};assert(!bno_native_inspect(0,&s));assert(!s.effect_count && !s.ever_resources);
        before=r;
    } else {
        assert(rc==(mode==1?NPP_CONFIGURE:NPP_READ));assert(!std::memcmp(&r,&before,sizeof r));
        assert(mode==1?control_allocs==1:control_allocs==0);
        assert(!sdk_allocations && !sdk_prepares && !sdk_completes && !sdk_resets && !sdk_live);
        if(mode==1)assert(w->configure_status==BOC_MAPPING); /* prefix reserves actual synthetic control PA */
    }
    int q=queries,c=control_allocs;
    assert(npp_native_configure_once(&e,w,&r)==NPP_USED && queries==q && control_allocs==c && !std::memcmp(&r,&before,sizeof r));
    std::free(w);
    std::puts("ACTUAL SAME-OWNER JOIN / HOST TEST-SDK ONLY PASS; no backing allocations/effects");return 0;
}
