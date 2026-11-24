/* Original synthetic callee faults; NOT native execution or policy provenance. */
#ifdef NDEBUG
#error tests require assertions
#endif
#include "private_policy_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct npp_workspace w;
static struct npp_expected expected;
static struct npp_result out,saved;
static struct npp_ops ops;
static struct npp_state state;
static struct nps_result facts;
static unsigned cases,reads,configures;
static int fault,reentry;
static struct bpl_policy observed;
/* Link traps: tests only invoke fresh internal states, never native global. */
int npr_read_native(const struct nps_expected *e,struct npr_workspace *a,struct nps_result *b)
{ (void)e;(void)a;(void)b;abort(); }
int boc_native_derive_and_configure(const struct bpl_policy *p,struct boc_workspace *a,struct boc_receipt *b)
{ (void)p;(void)a;(void)b;abort(); }
static void attempt_reentry(void)
{
    struct npp_result untouched=out;
    assert(npp_with_ops(&state,&ops,&expected,&w,&out)==NPP_BUSY);
    assert(!memcmp(&out,&untouched,sizeof out));reentry++;
}
static int read_callee(void *context,const struct nps_expected *e,struct npr_workspace *scratch,struct nps_result *r)
{
    assert(!context && scratch==&w.reader && !configures && reads++==0);
    assert(!memcmp(e,&expected.identity,sizeof *e));attempt_reentry();
    if(fault==1)return NPR_CLEANUP;
    *r=facts;
    if(fault==2)expected.irq_domain++;
    if(fault==3)ops.configure=NULL;
    return NPR_OK;
}
static int configure_callee(void *context,const struct bpl_policy *p,struct boc_workspace *scratch,struct boc_receipt *r)
{
    assert(!context && scratch==&w.control && reads==1 && configures++==0);
    assert(atomic_load(&state.phase)==NPP_CONFIGURING);attempt_reentry();observed=*p;
    if(fault==4)return BOC_MAPPING; /* can represent a legitimately reserved control page */
    memset(r,0,sizeof *r);r->version=1;r->complete=1;r->configured=1;
    r->page_count=2;r->query_count=4;r->base_policy=*p;r->final_policy=*p;
    if(fault==5)expected.platform_domain++;
    if(fault==6)ops.read=NULL;
    if(fault==7)((struct bpl_policy *)(uintptr_t)p)->irq_domain++;
    if(fault==8)r->configured=0;
    if(fault==9)r->complete=0;
    if(fault==10)r->page_count=0;
    if(fault==11)r->query_count=3;
    if(fault==12)r->base_policy.ram[0].bytes-=BPL_PAGE;
    return BOC_OK;
}
static void prepare(void)
{
    /* Fresh TEST state; production resident latch has no reset path. */
    atomic_init(&state.phase,NPP_EMPTY);memset(&w,0,sizeof w);memset(&facts,0,sizeof facts);
    memset(&expected,0,sizeof expected);memset(&observed,0,sizeof observed);
    for(unsigned i=0;i<NPS_NONCE_BYTES;i++)expected.identity.nonce[i]=(uint8_t)(i+1);
    for(unsigned i=0;i<NPS_IDENTITIES;i++)memset(expected.identity.sha256[i],(int)i+1,NPS_SHA_BYTES);
    expected.platform_domain=1;expected.dart_domain=2;expected.irq_domain=3;expected.profile=1;
    facts.version=facts.board_profile=facts.wire_profile=facts.coverage=1;
    facts.page_shift=14;facts.pa_bits=36;facts.identity=expected.identity;
    facts.envelope=(struct mcp_range){MCP_DRAM_BASE,0x1000000};
    facts.ram_count=2;facts.ram[0]=(struct mcp_range){MCP_DRAM_BASE,0x800000};
    facts.ram[1]=(struct mcp_range){MCP_DRAM_BASE+0x800000,0x800000};
    facts.union_count=1;facts.relevant_union[0]=(struct mcp_range){MCP_DRAM_BASE+1,1};
    ops=(struct npp_ops){NULL,0,read_callee,configure_callee};
    memset(&out,0xa5,sizeof out);saved=out;reads=configures=0;fault=reentry=0;
}
static void run(int wanted)
{
    int rc=npp_with_ops(&state,&ops,&expected,&w,&out);cases++;
    assert(rc==wanted);
    assert(atomic_load(&state.phase)==(wanted==NPP_OK?NPP_CONFIGURED:NPP_FAILED));
    if(wanted)assert(!memcmp(&out,&saved,sizeof out));
    else {
        assert(out.version==1 && out.profile==1 && out.ram_count==2 && out.control_pages==2 && out.queries==4);
        assert(observed.ram_count==2 && observed.ram[0].bytes==0x800000 && observed.ram[1].base==MCP_DRAM_BASE+0x800000);
        assert(!memcmp(&out.expected,&expected,sizeof expected));
    }
    unsigned old_reads=reads,old_configures=configures;
    /* A mutated table is repaired only in TEST storage; the same state still refuses. */
    ops.read=read_callee;ops.configure=configure_callee;
    assert(npp_with_ops(&state,&ops,&expected,&w,&out)==NPP_USED);
    assert(reads==old_reads && configures==old_configures);
}
int main(void)
{
    prepare();run(NPP_OK);assert(observed.reserved_count==1 && observed.reserved[0].base==MCP_DRAM_BASE && observed.reserved[0].bytes==BPL_PAGE);
    for(fault=1;fault<=12;) {
        int f=fault;prepare();fault=f;run(f==1?NPP_READ:f<=3?NPP_CHANGED:f==4?NPP_CONFIGURE:NPP_CHANGED);
        assert(configures==(f>=4));fault=f+1;
    }
    for(unsigned n=0;n<=8;n++) {
        prepare();facts.union_count=n;
        for(unsigned i=0;i<n;i++)facts.relevant_union[i]=(struct mcp_range){MCP_DRAM_BASE+(uint64_t)(n-i)*0x10000+1,0x4000};
        run(NPP_OK);assert(observed.reserved_count==n);
        for(unsigned i=0;i<n;i++)assert(observed.reserved[i].base==MCP_DRAM_BASE+(i+1)*0x10000 && observed.reserved[i].bytes==0x8000);
    }
    prepare();facts.union_count=9;run(NPP_POLICY);assert(!configures);
    prepare();facts.union_count=3;
    facts.relevant_union[0]=(struct mcp_range){MCP_DRAM_BASE+0x8001,0x100};
    facts.relevant_union[1]=(struct mcp_range){MCP_DRAM_BASE+1,1};
    facts.relevant_union[2]=(struct mcp_range){MCP_DRAM_BASE+0x4000,0x4001};
    run(NPP_OK);assert(observed.reserved_count==1 && observed.reserved[0].bytes==0xc000);
    prepare();facts.relevant_union[0]=(struct mcp_range){MCP_PA_LIMIT-1,1};run(NPP_OK);
    assert(observed.reserved[0].base==MCP_PA_LIMIT-BPL_PAGE && observed.reserved[0].bytes==BPL_PAGE);
    for(unsigned bit=0;bit<14;bit++) {
        prepare();uint64_t off=UINT64_C(1)<<bit;
        facts.relevant_union[0]=(struct mcp_range){MCP_DRAM_BASE+off,BPL_PAGE-off+1};run(NPP_OK);
        assert(observed.reserved[0].bytes==BPL_PAGE*2);
    }
    for(unsigned bad=0;bad<15;bad++) {
        prepare();
        switch(bad) {
        case 0:facts.ram_count=0;break;case 1:facts.ram_count=9;break;
        case 2:facts.ram[1].base-=BPL_PAGE;break;case 3:facts.ram[1].bytes++;break;
        case 4:facts.ram[0].bytes=0;break;case 5:facts.ram[2].bytes=1;break;
        case 6:facts.identity.nonce[0]^=1;break;case 7:facts.page_shift=12;break;
        case 8:facts.pa_bits=64;break;case 9:facts.coverage=2;break;
        case 10:facts.relevant_union[0].bytes=0;break;
        case 11:facts.relevant_union[0]=(struct mcp_range){MCP_PA_LIMIT-1,2};break;
        case 12:facts.relevant_union[0]=(struct mcp_range){UINT64_MAX,1};break;
        case 13:facts.envelope.bytes=0;break;case 14:facts.ram[1].base+=BPL_PAGE;break;
        }
        run(NPP_POLICY);assert(!configures);
    }
    /* Every public/internal object pair and context alias refuses before callbacks. */
    for(unsigned i=0;i<5;i++)for(unsigned j=0;j<5;j++)if(i!=j) {
        prepare();void *p[5]={&state,&ops,&expected,&w,&out};p[j]=p[i];
        assert(npp_with_ops(p[0],p[1],p[2],p[3],p[4])==NPP_ARGUMENT);
        assert(!reads && !configures);cases++;
    }
    for(unsigned i=0;i<5;i++) {
        prepare();void *p[5]={&state,&ops,&expected,&w,&out};ops.context=p[i];ops.context_bytes=1;
        assert(npp_with_ops(&state,&ops,&expected,&w,&out)==NPP_ARGUMENT && !reads);cases++;
        prepare();p[0]=&state;p[1]=&ops;p[2]=&expected;p[3]=&w;p[4]=&out;p[i]=NULL;
        assert(npp_with_ops(p[0],p[1],p[2],p[3],p[4])==NPP_ARGUMENT && !reads);cases++;
    }
    for(unsigned i=0;i<9;i++) {
        prepare();switch(i) {
        case 0:expected.profile=0;break;case 1:expected.reserved=1;break;
        case 2:expected.platform_domain=0;break;case 3:expected.dart_domain=0;break;case 4:expected.irq_domain=0;break;
        case 5:memset(expected.identity.nonce,0,NPS_NONCE_BYTES);break;
        case 6:memset(expected.identity.sha256[4],0,NPS_SHA_BYTES);break;
        case 7:ops.context=(void *)(uintptr_t)(UINTPTR_MAX-1);ops.context_bytes=3;break;
        case 8:ops.read=NULL;break;
        }
        assert(npp_with_ops(&state,&ops,&expected,&w,&out)==NPP_ARGUMENT && !reads);
        assert(atomic_load(&state.phase)==NPP_EMPTY && !memcmp(&out,&saved,sizeof out));cases++;
    }
    printf("private-policy HOST synthetic cases=%u; native-global never reset/invoked\n",cases);return 0;
}
