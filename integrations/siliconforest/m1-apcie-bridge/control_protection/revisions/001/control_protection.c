/* Original bounded page-set/receipt algorithm. No hardware write path. */
#include "control_protection.h"
#include <string.h>
#define PA_LIMIT UINT64_C(0x1000000000)
static const char PROVIDER[]="79b153f2de3c0899a1a59f42b2b2d24dd468588723ca9f75109d53d90269656b";
static const char CORE[]="c585fd8b80479975a46ad19c83b7610a53f3a4a1a420241cbe0022a2d1fb82fb";
static const char TYPES[]="51502d598c34ca88e0e6129fba88355cfc58e8de99fb7a9e6fae7c22b6321ca4";
static int span(uint64_t p,uint64_t n) {return n && p<=UINT64_MAX-n;}
static int overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t k)
{return !span(a,n)||!span(b,k)||(a<b+k && b<a+n);}
static int aliases(const void *a,size_t n,const void *b,size_t k)
{return overlap((uint64_t)(uintptr_t)a,n,(uint64_t)(uintptr_t)b,k);}
static int range_equal(struct bpl_range a,struct bpl_range b)
{return a.base==b.base && a.bytes==b.bytes;}
static int setup_equal(const struct bpn_native_setup *a,const struct bpn_native_setup *b)
{return a->identity==b->identity && a->generation==b->generation &&
 range_equal(a->typed_anchor_cpu,b->typed_anchor_cpu) && a->opaque_mutex_identity==b->opaque_mutex_identity;}
static int source_equal(const struct bcp_source *a,const struct bcp_source *b)
{return a->pmap_identity==b->pmap_identity && a->mutex_cpu==b->mutex_cpu &&
 a->page_shift==b->page_shift && a->page_size==b->page_size && a->native_page_size==b->native_page_size &&
 a->mutex_size==b->mutex_size && a->mutex_align==b->mutex_align;}
static int policy_equal(const struct bpl_policy *a,const struct bpl_policy *b)
{
    if(a->platform_domain!=b->platform_domain || a->dart_domain!=b->dart_domain || a->irq_domain!=b->irq_domain ||
       a->ram_count!=b->ram_count || a->reserved_count!=b->reserved_count)return 0;
    for(unsigned i=0;i<BPL_RANGES;i++)if(!range_equal(a->ram[i],b->ram[i]) || !range_equal(a->reserved[i],b->reserved[i]))return 0;
    return 1;
}
static int policy_valid(const struct bpl_policy *p)
{
    if(!p || !p->platform_domain || !p->dart_domain || !p->irq_domain || !p->ram_count ||
        p->ram_count>BPL_RANGES || p->reserved_count>BPL_RANGES)return 0;
    for(unsigned which=0;which<2;which++) {
        const struct bpl_range *v=which?p->reserved:p->ram;
        uint32_t count=which?p->reserved_count:p->ram_count;
        for(unsigned i=0;i<BPL_RANGES;i++) {
            if(i>=count){if(v[i].base || v[i].bytes)return 0;continue;}
            if(!span(v[i].base,v[i].bytes) || v[i].base+v[i].bytes>PA_LIMIT)return 0;
            for(unsigned j=0;j<i;j++)if(overlap(v[i].base,v[i].bytes,v[j].base,v[j].bytes))return 0;
        }
    }
    return 1;
}
int bcp_preflight(const struct bpl_policy *p,struct bcp_workspace *w,struct bcp_receipt *out)
{
    if(!p || !w || !out || aliases(p,sizeof *p,w,sizeof *w) || aliases(p,sizeof *p,out,sizeof *out) ||
        aliases(w,sizeof *w,out,sizeof *out))return BCP_ARGUMENT;
    return policy_valid(p)?BCP_OK:BCP_POLICY;
}
static int profile(const struct bpn_native_setup *s,const struct bcp_source *p)
{
    return p->pmap_identity && p->mutex_cpu==s->opaque_mutex_identity && !(p->mutex_cpu&7u) &&
        p->page_shift==14 && p->page_size==BPL_PAGE && p->native_page_size==BPL_PAGE &&
        p->mutex_size==16 && p->mutex_align==8 && span(p->mutex_cpu,16);
}
static void sort_ranges(struct bpl_range *v,unsigned n)
{
    for(unsigned i=1;i<n;i++)for(unsigned j=i;j && v[j].base<v[j-1].base;j--) {
        struct bpl_range t=v[j];v[j]=v[j-1];v[j-1]=t;
    }
}
static unsigned union_ranges(struct bpl_range *v,unsigned n)
{
    sort_ranges(v,n);unsigned count=0;
    for(unsigned i=0;i<n;i++) {
        if(count && v[i].base<=v[count-1].base+v[count-1].bytes) {
            uint64_t end=v[i].base+v[i].bytes,old=v[count-1].base+v[count-1].bytes;
            if(end>old)v[count-1].bytes=end-v[count-1].base;
        } else v[count++]=v[i];
    }
    return count;
}
static int canonical_original(const struct bpl_policy *original,const struct bpl_policy *canonical)
{
    if(!policy_valid(original) || !policy_valid(canonical) || original->platform_domain!=canonical->platform_domain ||
        original->dart_domain!=canonical->dart_domain || original->irq_domain!=canonical->irq_domain ||
        original->ram_count!=canonical->ram_count || original->reserved_count!=canonical->reserved_count)return 0;
    for(unsigned group=0;group<2;group++) {
        const struct bpl_range *a=group?original->reserved:original->ram,*b=group?canonical->reserved:canonical->ram;
        unsigned n=group?original->reserved_count:original->ram_count;
        for(unsigned i=0;i<n;i++) {
            if(i && b[i].base<=b[i-1].base)return 0;
            unsigned j;for(j=0;j<n;j++)if(range_equal(a[j],b[i]))break;
            if(j==n)return 0;
        }
    }
    return 1;
}
static int receipt_unions(const struct bcp_receipt *r)
{
    struct bpl_range ranges[BCP_PAGES+BPL_RANGES];
    if(r->protection.cpu_count>8 || r->protection.pa_count>8 || r->final_policy.reserved_count>8 ||
        r->final_policy.ram_count!=r->base_policy.ram_count ||
        r->final_policy.platform_domain!=r->base_policy.platform_domain ||
        r->final_policy.dart_domain!=r->base_policy.dart_domain || r->final_policy.irq_domain!=r->base_policy.irq_domain)return 0;
    for(unsigned i=0;i<8;i++)if(!range_equal(r->final_policy.ram[i],r->base_policy.ram[i]))return 0;
    for(unsigned group=0;group<3;group++) {
        for(unsigned i=0;i<r->page_count;i++)ranges[i]=(struct bpl_range){group?r->pa_pages[i]:r->cpu_pages[i],BPL_PAGE};
        unsigned input_count=r->page_count;
        if(group==2)for(unsigned i=0;i<r->base_policy.reserved_count;i++)ranges[input_count++]=r->base_policy.reserved[i];
        unsigned n=union_ranges(ranges,input_count);
        const struct bpl_range *out=group==0?r->protection.cpu:group==1?r->protection.pa:r->final_policy.reserved;
        unsigned count=group==0?r->protection.cpu_count:group==1?r->protection.pa_count:r->final_policy.reserved_count;
        if(n!=count || n>8)return 0;
        for(unsigned i=0;i<8;i++) {
            if(i<n){if(!range_equal(ranges[i],out[i]))return 0;}
            else if(out[i].base || out[i].bytes)return 0;
        }
    }
    return 1;
}
static int collect(struct bcp_workspace *w,struct bpl_range v)
{
    uint64_t at=v.base&~(BPL_PAGE-1),last=(v.base+v.bytes-1)&~(BPL_PAGE-1);
    for(;;) {
        if(!span(at,BPL_PAGE))return BCP_LIMIT;
        unsigned i;for(i=0;i<w->count;i++)if(w->pages[i]==at)break;
        if(i==w->count){if(w->count==BCP_PAGES)return BCP_LIMIT;w->pages[w->count++]=at;}
        if(at==last)break;
        if(at>UINT64_MAX-BPL_PAGE || at+BPL_PAGE<=at)return BCP_LIMIT;
        at+=BPL_PAGE;
    }
    return BCP_OK;
}
static int pa_valid(const struct bpl_policy *p,uint64_t pa)
{
    int contained=0;
    /* ORIGINAL RAM interval boundaries, before any sorting/union. */
    for(unsigned i=0;i<p->ram_count;i++) {
        struct bpl_range v=p->ram[i];
        if(pa>=v.base && BPL_PAGE<=v.bytes && pa-v.base<=v.bytes-BPL_PAGE)contained=1;
    }
    if(!contained)return 0;
    for(unsigned i=0;i<p->reserved_count;i++)if(overlap(pa,BPL_PAGE,p->reserved[i].base,p->reserved[i].bytes))return 0;
    return 1;
}
static int stable(const struct bpn_native_setup *s,const struct bpl_policy *p,const struct bcp_reads *ops,
                  const struct bcp_reads *captured,const struct bcp_workspace *w)
{
    struct bcp_source source={0};
    if(ops->context!=captured->context || ops->observe!=captured->observe || ops->lookup!=captured->lookup)return BCP_CHANGED;
    if(captured->observe(captured->context,w->original_setup.opaque_mutex_identity,&source))return BCP_NATIVE;
    if(ops->context!=captured->context || ops->observe!=captured->observe || ops->lookup!=captured->lookup)return BCP_CHANGED;
    if(!setup_equal(s,&w->original_setup) || !policy_equal(p,&w->original_policy) ||
        !source_equal(&source,&w->original_source))return BCP_CHANGED;
    return BCP_OK;
}
static int stage(const struct bpn_native_setup *s,const struct bpl_policy *p,const struct bcp_reads *ops,
                 struct bcp_workspace *w,struct bcp_receipt *out)
{
    int rc=bcp_preflight(p,w,out);if(rc)return rc;
    if(!s || !ops || !ops->observe || !ops->lookup ||
        aliases(s,sizeof *s,w,sizeof *w) || aliases(s,sizeof *s,out,sizeof *out) ||
        aliases(ops,sizeof *ops,w,sizeof *w) || aliases(ops,sizeof *ops,out,sizeof *out))return BCP_ARGUMENT;
    if(!s->identity || s->identity!=s->typed_anchor_cpu.base || !s->generation || !s->opaque_mutex_identity ||
        !span(s->typed_anchor_cpu.base,s->typed_anchor_cpu.bytes))return BCP_ARGUMENT;
    struct bcp_reads reads=*ops;
    /* Small pre-getter snapshots: scratch cannot be touched until the exact
     * mutex span is known and scratch/control alias checks have passed. */
    const struct bpn_native_setup captured_setup=*s;
    const struct bpl_policy captured_policy=*p;
    struct bcp_source source={0};
    if(reads.observe(reads.context,s->opaque_mutex_identity,&source))return BCP_NATIVE;
    if(!profile(s,&source))return BCP_PROFILE;
    if(!setup_equal(s,&captured_setup) || !policy_equal(p,&captured_policy) ||
        ops->context!=reads.context || ops->observe!=reads.observe || ops->lookup!=reads.lookup)return BCP_CHANGED;
    struct bpl_range mutex={source.mutex_cpu,source.mutex_size};
    if(overlap(s->typed_anchor_cpu.base,s->typed_anchor_cpu.bytes,mutex.base,mutex.bytes))return BCP_ARGUMENT;
    const struct bpl_range controls[2]={s->typed_anchor_cpu,mutex};
    for(unsigned i=0;i<2;i++) {
        struct bpl_range v=controls[i];
        if(overlap((uint64_t)(uintptr_t)w,sizeof *w,v.base,v.bytes) ||
           overlap((uint64_t)(uintptr_t)out,sizeof *out,v.base,v.bytes))return BCP_ARGUMENT;
    }
    /* No workspace writes before exact control/output/input aliases checked. */
    memset(w,0,sizeof *w);w->original_policy=captured_policy;w->original_setup=captured_setup;w->original_source=source;
    rc=collect(w,controls[0]);if(!rc)rc=collect(w,controls[1]);if(rc)return rc;
    for(unsigned i=1;i<w->count;i++)for(unsigned j=i;j && w->pages[j]<w->pages[j-1];j--) {
        uint64_t t=w->pages[j];w->pages[j]=w->pages[j-1];w->pages[j-1]=t;
    }
    for(unsigned pass=0;pass<2;pass++) {
        for(unsigned i=0;i<w->count;i++) {
            uint32_t ppnum=0;
            if(reads.lookup(reads.context,source.pmap_identity,w->pages[i],&ppnum))return BCP_NATIVE;
            if(!ppnum || ppnum>0x3fffffu)return BCP_MAPPING;
            uint64_t pa=(uint64_t)ppnum<<14;
            if(pa>PA_LIMIT-BPL_PAGE || !pa_valid(&w->original_policy,pa))return BCP_MAPPING;
            if(pass){w->second[i]=ppnum;if(ppnum!=w->first[i])return BCP_CHANGED;}
            else w->first[i]=ppnum;
        }
        rc=stable(s,p,ops,&reads,w);if(rc)return rc;
    }
    struct bcp_receipt *e=&w->expected;
    e->version=BCP_VERSION;e->setup=w->original_setup;e->source=source;e->mutex_span=mutex;
    memcpy(e->provider_freeze,PROVIDER,sizeof PROVIDER);memcpy(e->core_freeze,CORE,sizeof CORE);memcpy(e->type_profile,TYPES,sizeof TYPES);
    e->page_count=w->count;e->query_count=w->count*2;e->base_policy=w->original_policy;
    sort_ranges(e->base_policy.ram,e->base_policy.ram_count);sort_ranges(e->base_policy.reserved,e->base_policy.reserved_count);
    e->final_policy=e->base_policy;
    e->protection.provenance=s->identity; /* identity label, not authentication */
    for(unsigned i=0;i<w->count;i++) {
        e->cpu_pages[i]=w->pages[i];e->ppnums[i]=w->first[i];e->pa_pages[i]=(uint64_t)w->first[i]<<14;
        w->union_ranges[i]=(struct bpl_range){e->cpu_pages[i],BPL_PAGE};
    }
    unsigned n=union_ranges(w->union_ranges,w->count);e->protection.cpu_count=n;
    for(unsigned i=0;i<n;i++)e->protection.cpu[i]=w->union_ranges[i];
    for(unsigned i=0;i<w->count;i++)w->union_ranges[i]=(struct bpl_range){e->pa_pages[i],BPL_PAGE};
    n=union_ranges(w->union_ranges,w->count);e->protection.pa_count=n;
    for(unsigned i=0;i<n;i++)e->protection.pa[i]=w->union_ranges[i];
    for(unsigned i=0;i<e->base_policy.reserved_count;i++)w->union_ranges[n+i]=e->base_policy.reserved[i];
    n=union_ranges(w->union_ranges,n+e->base_policy.reserved_count);if(n>BPL_RANGES)return BCP_LIMIT;
    memset(e->final_policy.reserved,0,sizeof e->final_policy.reserved);e->final_policy.reserved_count=n;
    for(unsigned i=0;i<n;i++)e->final_policy.reserved[i]=w->union_ranges[i];
    e->protection.complete=1;e->complete=1;
    w->candidate=*e;
    return BCP_OK;
}
int bcp_check_workspace(const struct bcp_workspace *w)
{
    if(!w)return BCP_ARGUMENT;
    const struct bcp_receipt *a=&w->expected,*b=&w->candidate;
    if(a->version!=BCP_VERSION || a->complete!=1 || a->configured || a->page_count<1 || a->page_count>8 || a->page_count!=w->count || a->query_count!=a->page_count*2 ||
       !setup_equal(&a->setup,&w->original_setup) || !source_equal(&a->source,&w->original_source) || !profile(&a->setup,&a->source) ||
       !canonical_original(&w->original_policy,&a->base_policy) ||
       a->mutex_span.base!=a->source.mutex_cpu || a->mutex_span.bytes!=16 ||
       b->version!=a->version || b->complete!=a->complete || b->configured!=a->configured || b->page_count!=a->page_count || b->query_count!=a->query_count ||
       memcmp(a->provider_freeze,PROVIDER,sizeof PROVIDER) || memcmp(a->core_freeze,CORE,sizeof CORE) || memcmp(a->type_profile,TYPES,sizeof TYPES) ||
       memcmp(a->provider_freeze,b->provider_freeze,sizeof a->provider_freeze) || memcmp(a->core_freeze,b->core_freeze,sizeof a->core_freeze) ||
       memcmp(a->type_profile,b->type_profile,sizeof a->type_profile) || !setup_equal(&a->setup,&b->setup) || !source_equal(&a->source,&b->source) ||
       !range_equal(a->mutex_span,b->mutex_span) || !policy_equal(&a->base_policy,&b->base_policy) || !policy_equal(&a->final_policy,&b->final_policy) ||
       a->protection.complete!=1 || b->protection.complete!=1 || a->protection.provenance!=a->setup.identity ||
       b->protection.provenance!=a->protection.provenance || b->protection.pa_count!=a->protection.pa_count || b->protection.cpu_count!=a->protection.cpu_count)return BCP_BINDING;
    for(unsigned i=0;i<8;i++) {
        if(a->cpu_pages[i]!=b->cpu_pages[i] || a->ppnums[i]!=b->ppnums[i] || a->pa_pages[i]!=b->pa_pages[i] ||
            !range_equal(a->protection.cpu[i],b->protection.cpu[i]) || !range_equal(a->protection.pa[i],b->protection.pa[i]))return BCP_BINDING;
        if(i<a->page_count) {
            if(a->cpu_pages[i]!=w->pages[i] || a->ppnums[i]!=w->first[i] || w->first[i]!=w->second[i] ||
                !a->ppnums[i] || a->ppnums[i]>0x3fffffu || a->pa_pages[i]!=((uint64_t)a->ppnums[i]<<14))return BCP_BINDING;
        } else if(a->cpu_pages[i] || a->ppnums[i] || a->pa_pages[i])return BCP_BINDING;
    }
    if(!receipt_unions(a))return BCP_BINDING;
    return BCP_OK;
}
int bcp_derive(const struct bpn_native_setup *s,const struct bpl_policy *p,const struct bcp_reads *ops,
               struct bcp_workspace *w,struct bcp_receipt *out)
{
    int rc=stage(s,p,ops,w,out);if(!rc)rc=bcp_check_workspace(w);
    if(!rc)*out=w->candidate;
    return rc;
}
int bcp_boot_configure(const struct bpl_policy *p,const struct bcp_boot_ops *ops,
                       struct bcp_workspace *w,struct bcp_receipt *out)
{
    int rc=bcp_preflight(p,w,out);if(rc)return rc;
    if(!ops || !ops->pin_empty || !ops->configure || !ops->reads.observe || !ops->reads.lookup ||
        aliases(ops,sizeof *ops,w,sizeof *w) || aliases(ops,sizeof *ops,out,sizeof *out))return BCP_ARGUMENT;
    /* No caller-supplied setup: the trusted native callback owns pin_empty. */
    struct bpn_native_setup setup={0};
    const struct bcp_boot_ops captured=*ops;
    const struct bpl_policy captured_policy=*p;
    if(captured.pin_empty(captured.reads.context,&setup))return BCP_NATIVE;
    if(!policy_equal(p,&captured_policy))return BCP_CHANGED;
    rc=stage(&setup,p,&captured.reads,w,out);if(rc)return rc;
    if(ops->pin_empty!=captured.pin_empty || ops->configure!=captured.configure ||
        ops->reads.context!=captured.reads.context || ops->reads.observe!=captured.reads.observe || ops->reads.lookup!=captured.reads.lookup ||
        !setup_equal(&setup,&w->expected.setup) || !policy_equal(p,&w->original_policy))return BCP_CHANGED;
    rc=bcp_check_workspace(w);if(rc)return rc;
    if(captured.configure(captured.reads.context,&w->candidate.setup,&w->candidate.final_policy,&w->candidate.protection))return BCP_NATIVE;
    rc=bcp_check_workspace(w);if(rc)return rc;
    if(!policy_equal(p,&w->original_policy))return BCP_CHANGED;
    w->candidate.configured=1;*out=w->candidate;return BCP_OK;
}
