/* Original algorithm fixtures; numeric addresses are not native observations. */
#include "owned_control.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#error "control tests require assertions"
#endif
static unsigned checks;
#define CHECK(x) do{checks++;assert(x);}while(0)
struct fixture {
    struct bno_native_setup setup;
    struct bpl_policy policy;
    struct boc_source source;
    struct boc_workspace workspace;
    struct boc_receipt out,saved;
    struct boc_boot_ops ops;
    struct bpl_registry admitted;
    unsigned observes,queries,pins,configures,fail_query,change_query,change_observe,change_kind;
    unsigned alias_pa,zero_ppn,high_ppn,top_ppn,pin_failure,configure_failure,configure_mutation;
    uint64_t va[16];
};
static struct fixture *f;
static int observe(void *ctx,uint64_t opaque,struct boc_source *out)
{
    struct fixture *x=ctx;CHECK(opaque==x->setup.opaque_mutex_identity);x->observes++;
    *out=x->source;
    if(x->change_observe==x->observes)switch(x->change_kind) {
    case 0:out->pmap_identity++;break;case 1:out->mutex_cpu+=8;break;
    case 2:out->page_shift=12;break;case 3:out->page_size=4096;break;
    case 4:out->native_page_size=4096;break;case 5:out->mutex_size=8;break;
    case 6:out->mutex_align=16;break;case 7:x->setup.generation++;break;
    case 8:x->policy.platform_domain++;break;case 9:x->ops.reads.context=NULL;break;
    }
    return 0;
}
static int lookup(void *ctx,uint64_t map,uint64_t va,uint32_t *ppnum)
{
    struct fixture *x=ctx;CHECK(!(va&(BPL_PAGE-1)));if(map!=x->source.pmap_identity)return -1;
    CHECK(x->queries<16);x->va[x->queries++]=va;
    if(x->fail_query==x->queries)return -1;
    uint32_t n=x->alias_pa?0:(uint32_t)((va>>14)&31u);
    *ppnum=x->top_ppn?0x3fffffu:0x200000u+n*2;
    if(x->zero_ppn)*ppnum=0;if(x->high_ppn)*ppnum=0x400000u;
    if(x->change_query==x->queries)(*ppnum)++;
    return 0;
}
static int pin(void *ctx,struct bno_native_setup *out)
{
    struct fixture *x=ctx;x->pins++;if(x->pins!=1 || x->pin_failure)return -1;
    *out=x->setup;return 0;
}
static int configure(void *ctx,const struct bno_native_setup *s,const struct bpl_policy *p,const struct bno_protection *protection)
{
    struct fixture *x=ctx;x->configures++;
    CHECK(s->identity==x->setup.identity && s->generation==x->setup.generation);
    CHECK(p->platform_domain==x->policy.platform_domain && p->ram_count==x->policy.ram_count);
    CHECK(protection->complete==1 && protection->provenance==s->identity && protection->pa_count && protection->cpu_count);
    CHECK(p==&x->workspace.candidate.final_policy && protection==&x->workspace.candidate.protection);
    if(!x->configure_failure)CHECK(bpl_init(&x->admitted,p)==BPL_OK);
    if(x->configure_mutation)x->workspace.candidate.protection.provenance++;
    return x->configure_failure?-1:0;
}
static void fresh(void)
{
    if(f)free(f);f=calloc(1,sizeof *f);CHECK(f);
    f->setup=(struct bno_native_setup){0x100008,1,{0x100008,0x5000},0x203ff8};
    f->source=(struct boc_source){0x9988,0x203ff8,14,16384,16384,16,8};
    f->policy.platform_domain=1;f->policy.dart_domain=2;f->policy.irq_domain=3;
    f->policy.ram_count=1;f->policy.reserved_count=1;
    f->policy.ram[0]=(struct bpl_range){UINT64_C(0x800000000),0x1000000};
    f->policy.reserved[0]=(struct bpl_range){UINT64_C(0x800f00000),BPL_PAGE};
    f->ops=(struct boc_boot_ops){{f,observe,lookup},pin,configure};
    memset(&f->out,0xa5,sizeof f->out);f->saved=f->out;
}
static int derive(void)
{return boc_derive(&f->setup,&f->policy,&f->ops.reads,&f->workspace,&f->out);}
static void unchanged(void){CHECK(!memcmp(&f->out,&f->saved,sizeof f->out));}
static void healthy_and_sets(void)
{
    fresh();CHECK(derive()==0);CHECK(!strcmp(f->out.provider_freeze,"0ee2101c9cda5cf282c3db48b7640cef98146c3b04724e3fe59138aaad772434"));CHECK(f->out.complete==1 && !f->out.configured && f->out.page_count==4 && f->out.query_count==8);
    CHECK(f->queries==8 && f->observes==3 && f->out.protection.cpu_count==2);
    CHECK(f->out.mutex_span.bytes==16 && f->out.cpu_pages[2]==0x200000 && f->out.cpu_pages[3]==0x204000);
    CHECK(f->out.protection.pa_count==2); /* two distinct VAs alias each PA */
    CHECK(f->out.pa_pages[0]==UINT64_C(0x800000000));
    CHECK(f->out.pa_pages[1]==UINT64_C(0x800008000));
    CHECK(f->out.final_policy.reserved_count==3); /* no bridging16KiB holes */
    CHECK(f->out.final_policy.reserved[2].base==UINT64_C(0x800f00000));
    for(unsigned i=0;i<4;i++)CHECK(f->va[i]==f->va[i+4]);
    fresh();f->alias_pa=1;CHECK(derive()==0);CHECK(f->out.page_count==4 && f->out.protection.pa_count==1);
    fresh();f->setup.typed_anchor_cpu.bytes=8;f->setup.opaque_mutex_identity=0x100100;f->source.mutex_cpu=0x100100;
    CHECK(derive()==0);CHECK(f->queries==2 && f->out.page_count==1 && f->out.protection.cpu_count==1);
    for(unsigned pages=1;pages<=8;pages++) {
        fresh();f->setup.identity=f->setup.typed_anchor_cpu.base=0x100000;f->setup.typed_anchor_cpu.bytes=pages*BPL_PAGE-16;
        f->policy.reserved_count=0;f->policy.reserved[0]=(struct bpl_range){0};
        f->setup.opaque_mutex_identity=0x100000+pages*BPL_PAGE-16;f->source.mutex_cpu=f->setup.opaque_mutex_identity;
        CHECK(derive()==0);CHECK(f->out.page_count==pages && f->queries==pages*2);
    }
    fresh();f->top_ppn=1;f->policy.ram[0]=(struct bpl_range){0,UINT64_C(0x1000000000)};
    CHECK(derive()==0);CHECK(f->out.pa_pages[0]==UINT64_C(0xfffffc000));
    fresh();f->policy.ram_count=2;f->policy.ram[0]=(struct bpl_range){UINT64_C(0x800008000),0xf0000};
    f->policy.ram[1]=(struct bpl_range){UINT64_C(0x800000000),0x8000};
    CHECK(derive()==0);CHECK(f->out.base_policy.ram[0].base==UINT64_C(0x800000000) && f->out.base_policy.ram_count==2);
    fresh();f->policy.reserved[0]=(struct bpl_range){UINT64_C(0x800004000),BPL_PAGE};
    CHECK(derive()==0);CHECK(f->out.final_policy.reserved_count==1 && f->out.final_policy.reserved[0].bytes==3*BPL_PAGE);
}
static void failures(void)
{
    fresh();f->zero_ppn=1;f->policy.ram[0]=(struct bpl_range){0,UINT64_C(0x1000000000)};
    CHECK(derive()==BOC_MAPPING && f->queries==1);unchanged();
    fresh();f->change_observe=1;f->change_kind=8;CHECK(derive()==BOC_CHANGED && !f->queries);unchanged();
    for(unsigned n=1;n<=8;n++) {fresh();f->fail_query=n;CHECK(derive()==BOC_NATIVE);unchanged();CHECK(f->queries==n);}
    for(unsigned n=1;n<=8;n++) {fresh();f->change_query=n;CHECK(derive()==BOC_CHANGED);unchanged();CHECK(f->queries<=8);}
    for(unsigned n=1;n<=3;n++)for(unsigned field=0;field<10;field++) {
        fresh();f->change_observe=n;f->change_kind=field;CHECK(derive()!=0);unchanged();CHECK(f->queries<=8);
    }
    for(unsigned kind=0;kind<16;kind++) {
        fresh();switch(kind) {
        case 0:f->setup.typed_anchor_cpu.bytes=0;break;
        case 1:f->setup.typed_anchor_cpu.bytes=UINT64_MAX;break;
        case 2:f->setup.generation=0;break;
        case 3:f->setup.identity++;break;
        case 4:f->setup.opaque_mutex_identity=0;break;
        case 5:f->setup.opaque_mutex_identity++;f->source.mutex_cpu++;break;
        case 6:f->setup.opaque_mutex_identity=0x100008;f->source.mutex_cpu=0x100008;break;
        case 7:f->setup.typed_anchor_cpu.bytes=9*BPL_PAGE;break;
        case 8:f->zero_ppn=1;break;case 9:f->high_ppn=1;break;
        case 10:f->source.pmap_identity=0;break;
        case 11:f->policy.reserved[0].base=UINT64_C(0x800000000);break;
        case 12:f->policy.ram[0].base=UINT64_C(0x900000000);break;
        case 13:f->policy.ram_count=2;f->policy.ram[0].bytes=8192;f->policy.ram[1]=(struct bpl_range){UINT64_C(0x800002000),0xffe000};break;
        case 14:f->setup.typed_anchor_cpu.base=f->setup.identity=UINT64_MAX-7;f->setup.typed_anchor_cpu.bytes=7;break;
        case 15:f->setup.opaque_mutex_identity=UINT64_MAX-7;f->source.mutex_cpu=UINT64_MAX-7;break;
        }
        CHECK(derive()!=0);unchanged();CHECK(f->queries<=16);
        if(kind==7)CHECK(!f->queries);
    }
    fresh();f->policy.reserved_count=8;
    for(unsigned i=0;i<8;i++)f->policy.reserved[i]=(struct bpl_range){UINT64_C(0x800100000)+i*0x8000,BPL_PAGE};
    CHECK(derive()==BOC_LIMIT);unchanged();
}
static void alias_and_preflight(void)
{
    fresh();memset(&f->workspace,0x5a,sizeof f->workspace);
    CHECK(boc_derive(&f->setup,&f->policy,&f->ops.reads,&f->workspace,&f->workspace.candidate)==BOC_ARGUMENT);
    CHECK(((unsigned char*)&f->workspace)[0]==0x5a && !f->observes);
    fresh();f->setup.identity=f->setup.typed_anchor_cpu.base=(uint64_t)(uintptr_t)&f->workspace;f->setup.typed_anchor_cpu.bytes=sizeof f->workspace;
    memset(&f->workspace,0x5a,sizeof f->workspace);CHECK(derive()==BOC_ARGUMENT);CHECK(((unsigned char*)&f->workspace)[0]==0x5a);unchanged();
    fresh();f->setup.opaque_mutex_identity=(uint64_t)(uintptr_t)&f->workspace;f->source.mutex_cpu=f->setup.opaque_mutex_identity;
    memset(&f->workspace,0x5a,sizeof f->workspace);CHECK(derive()==BOC_ARGUMENT);CHECK(((unsigned char*)&f->workspace)[0]==0x5a);unchanged();
    fresh();f->setup.identity=f->setup.typed_anchor_cpu.base=(uint64_t)(uintptr_t)&f->out;f->setup.typed_anchor_cpu.bytes=sizeof f->out;
    CHECK(derive()==BOC_ARGUMENT);unchanged();
    fresh();f->policy.ram_count=0;CHECK(boc_boot_configure(&f->policy,&f->ops,&f->workspace,&f->out)==BOC_POLICY);
    CHECK(!f->pins && !f->queries && !f->configures);unchanged();
    fresh();f->policy.ram[7].bytes=1;CHECK(derive()==BOC_POLICY);CHECK(!f->observes);unchanged();
}
static void receipt_mixing(void)
{
    for(unsigned i=0;i<27;i++) {
        fresh();CHECK(derive()==0);struct boc_receipt *r=&f->workspace.candidate;
        switch(i) {
        case 0:r->setup.identity++;break;case 1:r->setup.generation++;break;
        case 2:r->setup.typed_anchor_cpu.bytes++;break;case 3:r->setup.opaque_mutex_identity++;break;
        case 4:r->source.pmap_identity++;break;case 5:r->source.page_shift=12;break;
        case 6:r->mutex_span.bytes=8;break;case 7:r->page_count--;break;
        case 8:r->query_count--;break;case 9:r->cpu_pages[0]++;break;
        case 10:r->pa_pages[0]++;break;case 11:r->ppnums[0]++;break;
        case 12:r->base_policy.platform_domain++;break;case 13:r->final_policy.dart_domain++;break;
        case 14:r->base_policy.ram[0].bytes++;break;case 15:r->final_policy.reserved_count--;break;
        case 16:r->final_policy.reserved[0].bytes+=BPL_PAGE;break;case 17:r->protection.complete=0;break;
        case 18:r->protection.provenance++;break;case 19:r->protection.pa_count--;break;
        case 20:r->protection.cpu_count--;break;case 21:r->protection.pa[0].base++;break;
        case 22:r->provider_freeze[0]^=1;break;case 23:r->core_freeze[0]^=1;break;
        case 24:r->type_profile[0]^=1;break;case 25:r->configured=1;break;case 26:r->complete=0;break;
        }
        CHECK(boc_check_workspace(&f->workspace)==BOC_BINDING);CHECK(!f->configures);
    }
}
static void new_owner_binding(void)
{
    fresh();CHECK(derive()==0);
    const char old[]="79b153f2de3c0899a1a59f42b2b2d24dd468588723ca9f75109d53d90269656b";
    memcpy(f->workspace.expected.provider_freeze,old,sizeof old);
    memcpy(f->workspace.candidate.provider_freeze,old,sizeof old);
    CHECK(boc_check_workspace(&f->workspace)==BOC_BINDING && !f->configures);
    fresh();CHECK(derive()==0);
    /* Even matching candidate/expected claims cannot shorten the held setup. */
    f->workspace.expected.setup.typed_anchor_cpu.bytes--;
    f->workspace.candidate.setup=f->workspace.expected.setup;
    CHECK(boc_check_workspace(&f->workspace)==BOC_BINDING && !f->configures);
}
static void boot(void)
{
    fresh();CHECK(boc_boot_configure(&f->policy,&f->ops,&f->workspace,&f->out)==0);
    CHECK(f->pins==1 && f->configures==1 && f->out.configured==1 && f->queries==8);
    f->saved=f->out;CHECK(boc_boot_configure(&f->policy,&f->ops,&f->workspace,&f->out)==BOC_NATIVE);unchanged();CHECK(f->configures==1 && f->queries==8);
    fresh();f->pin_failure=1;CHECK(boc_boot_configure(&f->policy,&f->ops,&f->workspace,&f->out)==BOC_NATIVE);unchanged();CHECK(!f->queries && !f->configures);
    fresh();f->configure_failure=1;CHECK(boc_boot_configure(&f->policy,&f->ops,&f->workspace,&f->out)==BOC_NATIVE);unchanged();CHECK(f->pins==1 && f->configures==1);
    fresh();f->configure_mutation=1;CHECK(boc_boot_configure(&f->policy,&f->ops,&f->workspace,&f->out)==BOC_BINDING);unchanged();
    fresh();f->fail_query=1;CHECK(boc_boot_configure(&f->policy,&f->ops,&f->workspace,&f->out)==BOC_NATIVE);unchanged();CHECK(f->pins==1 && !f->configures);
}
int main(void)
{
    healthy_and_sets();failures();alias_and_preflight();receipt_mixing();new_owner_binding();boot();free(f);
    printf("CONTROL PROTECTION HOST PASS checks=%u; no native CPU translation executed\n",checks);return 0;
}
