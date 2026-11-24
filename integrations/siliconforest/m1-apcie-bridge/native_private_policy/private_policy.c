/* Original single-attempt composition; no native address or hardware writer. */
#include "private_policy_internal.h"
#include <string.h>
static struct npp_state native_state;
static int extent(const void *p,size_t n)
{ return p && n && (uintptr_t)p<=UINTPTR_MAX-n; }
static int overlap(const void *a,size_t n,const void *b,size_t k)
{ return (uintptr_t)a<(uintptr_t)b+k && (uintptr_t)b<(uintptr_t)a+n; }
static int nonzero(const uint8_t *p,size_t n)
{ for(size_t i=0;i<n;i++)if(p[i])return 1;return 0; }
static int expected_ok(const struct npp_expected *p)
{
    if(p->profile!=NPP_VM_PRIVATE_PROFILE || p->reserved || !p->platform_domain ||
       !p->dart_domain || !p->irq_domain || !nonzero(p->identity.nonce,NPS_NONCE_BYTES))return 0;
    for(unsigned i=0;i<NPS_IDENTITIES;i++)if(!nonzero(p->identity.sha256[i],NPS_SHA_BYTES))return 0;
    return 1;
}
static int expected_equal(const struct npp_expected *a,const struct npp_expected *b)
{
    return !memcmp(&a->identity,&b->identity,sizeof a->identity) &&
        a->platform_domain==b->platform_domain && a->dart_domain==b->dart_domain &&
        a->irq_domain==b->irq_domain && a->profile==b->profile && a->reserved==b->reserved;
}
static int ops_equal(const struct npp_ops *a,const struct npp_ops *b)
{ return a->context==b->context && a->context_bytes==b->context_bytes && a->read==b->read && a->configure==b->configure; }
static int range_ok(struct mcp_range v)
{ return v.bytes && v.base<MCP_PA_LIMIT && v.bytes<=MCP_PA_LIMIT-v.base; }
static int inside(struct mcp_range a,struct mcp_range b)
{ return b.base>=a.base && b.bytes<=a.bytes && b.base-a.base<=a.bytes-b.bytes; }
static int build_policy(struct npp_workspace *w)
{
    const struct nps_result *f=&w->facts;
    if(f->version!=1 || f->board_profile!=1 || f->page_shift!=14 || f->pa_bits!=36 ||
       f->wire_profile!=1 || f->coverage!=MCP_FOUR_PLANES_FOUR_TZ_ONLY ||
       memcmp(&f->identity,&w->captured.identity,sizeof f->identity) ||
       !f->ram_count || f->ram_count>NPS_RAM_MAX || f->union_count>NPS_UNION_MAX ||
       !range_ok(f->envelope) || ((f->envelope.base|f->envelope.bytes)&(BPL_PAGE-1)))return NPP_POLICY;
    memset(&w->policy,0,sizeof w->policy);
    w->policy.platform_domain=w->captured.platform_domain;
    w->policy.dart_domain=w->captured.dart_domain;w->policy.irq_domain=w->captured.irq_domain;
    w->policy.ram_count=f->ram_count;
    for(unsigned i=0;i<NPS_RAM_MAX;i++) {
        struct mcp_range r=f->ram[i];
        if(i>=f->ram_count) {if(r.base || r.bytes)return NPP_POLICY;continue;}
        if(!range_ok(r) || ((r.base|r.bytes)&(BPL_PAGE-1)) || !inside(f->envelope,r) ||
           (i && r.base<f->ram[i-1].base+f->ram[i-1].bytes))return NPP_POLICY;
        w->policy.ram[i]=(struct bpl_range){r.base,r.bytes};
    }
    for(unsigned i=0;i<f->union_count;i++) {
        struct mcp_range r=f->relevant_union[i];
        if(!range_ok(r))return NPP_POLICY;
        uint64_t end=r.base+r.bytes;
        /* PA_LIMIT is page-aligned: validated endpoint makes this add safe. */
        uint64_t rounded_end=(end+BPL_PAGE-1)&~(BPL_PAGE-1);
        uint64_t base=r.base&~(BPL_PAGE-1);
        if(rounded_end>MCP_PA_LIMIT)return NPP_POLICY;
        w->rounded[i]=(struct bpl_range){base,rounded_end-base};
    }
    for(unsigned i=1;i<f->union_count;i++)for(unsigned j=i;j && w->rounded[j].base<w->rounded[j-1].base;j--) {
        struct bpl_range t=w->rounded[j];w->rounded[j]=w->rounded[j-1];w->rounded[j-1]=t;
    }
    unsigned count=0;
    for(unsigned i=0;i<f->union_count;i++) {
        struct bpl_range r=w->rounded[i];
        if(count && r.base<=w->policy.reserved[count-1].base+w->policy.reserved[count-1].bytes) {
            struct bpl_range *last=&w->policy.reserved[count-1];
            if(r.base+r.bytes>last->base+last->bytes)last->bytes=r.base+r.bytes-last->base;
        } else {
            if(count==BPL_RANGES)return NPP_POLICY;
            w->policy.reserved[count++]=r;
        }
    }
    w->policy.reserved_count=count;return NPP_OK;
}
int npp_with_ops(struct npp_state *state,const struct npp_ops *operations,
                 const struct npp_expected *expected,struct npp_workspace *w,struct npp_result *out)
{
    const void *p[5]={state,operations,expected,w,out};
    const size_t n[5]={sizeof *state,sizeof *operations,sizeof *expected,sizeof *w,sizeof *out};
    for(unsigned i=0;i<5;i++) {
        if(!extent(p[i],n[i]))return NPP_ARGUMENT;
        for(unsigned j=0;j<i;j++)if(overlap(p[i],n[i],p[j],n[j]))return NPP_ARGUMENT;
    }
    if(!operations->read || !operations->configure || !expected_ok(expected))return NPP_ARGUMENT;
    if(operations->context || operations->context_bytes) {
        if(!extent(operations->context,operations->context_bytes))return NPP_ARGUMENT;
        for(unsigned i=0;i<5;i++)if(overlap(operations->context,operations->context_bytes,p[i],n[i]))return NPP_ARGUMENT;
    }
    unsigned empty=NPP_EMPTY;
    if(!atomic_compare_exchange_strong_explicit(&state->phase,&empty,NPP_READING,memory_order_acq_rel,memory_order_acquire))
        return empty==NPP_CONFIGURED || empty==NPP_FAILED?NPP_USED:NPP_BUSY;
    const struct npp_ops ops=*operations;
    w->captured=*expected;
    int rc=NPP_READ;
    w->reader_status=ops.read(ops.context,&w->captured.identity,&w->reader,&w->facts);
    w->configure_status=0;
    if(w->reader_status)goto failed;
    if(!expected_equal(expected,&w->captured) || !ops_equal(operations,&ops)) {rc=NPP_CHANGED;goto failed;}
    atomic_store_explicit(&state->phase,NPP_BUILDING,memory_order_release);
    rc=build_policy(w);if(rc)goto failed;
    w->policy_before=w->policy;
    atomic_store_explicit(&state->phase,NPP_CONFIGURING,memory_order_release);
    w->configure_status=ops.configure(ops.context,&w->policy,&w->control,&w->receipt);
    if(w->configure_status) {rc=NPP_CONFIGURE;goto failed;}
    if(!expected_equal(expected,&w->captured) || !ops_equal(operations,&ops) ||
       memcmp(&w->policy,&w->policy_before,sizeof w->policy)) {rc=NPP_CHANGED;goto failed;}
    /* Only the actual fixed configurator can establish these result facts.
     * They are checked for join consistency, not detached-receipt authority. */
    if(w->receipt.version!=BOC_VERSION || w->receipt.configured!=1 || w->receipt.complete!=1 ||
       !w->receipt.page_count || w->receipt.page_count>BOC_PAGES ||
       w->receipt.query_count!=2*w->receipt.page_count ||
       memcmp(&w->receipt.base_policy,&w->policy,sizeof w->policy)) {rc=NPP_CHANGED;goto failed;}
    memset(&w->staged,0,sizeof w->staged);
    w->staged.version=1;w->staged.profile=NPP_VM_PRIVATE_PROFILE;
    w->staged.ram_count=w->receipt.final_policy.ram_count;
    w->staged.reserved_count=w->receipt.final_policy.reserved_count;
    w->staged.control_pages=w->receipt.page_count;w->staged.queries=w->receipt.query_count;
    w->staged.expected=w->captured;
    *out=w->staged;
    atomic_store_explicit(&state->phase,NPP_CONFIGURED,memory_order_release);
    return NPP_OK;
failed:
    atomic_store_explicit(&state->phase,NPP_FAILED,memory_order_release);
    return rc;
}
static int fixed_read(void *unused,const struct nps_expected *e,struct npr_workspace *w,struct nps_result *r)
{ (void)unused;return npr_read_native(e,w,r); }
static int fixed_configure(void *unused,const struct bpl_policy *p,struct boc_workspace *w,struct boc_receipt *r)
{ (void)unused;return boc_native_derive_and_configure(p,w,r); }
int npp_native_configure_once(const struct npp_expected *e,struct npp_workspace *w,struct npp_result *r)
{
    const struct npp_ops ops={NULL,0,fixed_read,fixed_configure};
    return npp_with_ops(&native_state,&ops,e,w,r);
}
