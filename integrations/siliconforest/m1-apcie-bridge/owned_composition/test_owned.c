/* Original host-only lease/effect composition. Never a native backing join. */
#include "bcm4378_transport.h"
#include "bcm4378_identity_fw.h"
#include "bcm4378_fake_dart.h"
#include "bcm4378_fake_irq.h"
#include "bcm4378_fake_ring.h"
#include "bcm4378_fake_item.h"
#include "private_lifecycle/lifecycle.h"
#include "linkup.h"
#include "perst_gpio.h"
#include "radio_enable.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "owned composition requires assertions"
#endif
#if defined(BCO_HOST) && defined(KERNEL)
#error "owned composition is host-only"
#endif
#ifdef BCO_HOST
#include "register_accessor.h"
enum { SH=BCT_RAM_BASE+0x100, INFO=BCT_RAM_BASE+0x300, DESC=BCT_RAM_BASE+0x400,
       WI=BCT_RAM_BASE+0x500, RI=BCT_RAM_BASE+0x504, HEALTHY_EFFECTS=12 };
static _Alignas(16384) uint8_t root[16384],leaf[16384],data[16384];
static const uint8_t ITEM[8]={0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88};
static const struct bct_plan TP={0x10000,0x18000000,14,0xfeed0000};
static const struct bci_profile CP={0x18000000,0xffff,0xf0000,16,0xf0000000,28,0x4378,1};
static const struct dld_range RAM[]={{UINT64_C(0x800000000),0x2000000}};
static const struct dld_range RESERVED[]={{UINT64_C(0x800080000),DLD_PAGE}};
static const struct dld_policy DP={{DLD_APCIE_BASE,DLD_APCIE_SIZE},RAM,1,RESERVED,1};
static const struct bmi_profile IP={UINT64_C(0xfffff000),0x100,0,8,704};
static const struct bpl_request REQUEST={0x100,3,1};
struct event { uint32_t sequence,kind,resources; int raw,journal; };
static struct fixture {
    struct bpl_registry registry;
    struct bpl_lease lease;
    struct bpl_ticket ticket;
    struct apr_accessor accessor;
    struct apb_io io;
    struct bct_session transport;
    struct bci_identity identity;
    struct bci_fw_plan firmware;
    struct bci_request_record request;
    struct bfd_map map;
    struct bfd_workspace workspace;
    struct bmi_source irq;
    struct bcr_ring ring;
    uint32_t config[1024],bar0[8192],bar2[16384];
    uint32_t phy,run,link,pending_kind,pending_resources;
    unsigned calls,writes,bells,fw_calls,endpoint_reads,fail_at,apply_failed_write,reset_on_index,link_loss_on_index;
    unsigned logical_complete,pa_checked,stimuli,event_count,force_unknown;
    int last_raw,last_journal;
    struct event events[32];
} F;
static int snapshot(struct bpl_snapshot *s) { return bpl_inspect(&F.registry,&F.lease,s); }
static void pending(uint32_t kind,uint32_t resources)
{
    struct bpl_snapshot s;
    assert(snapshot(&s)==BPL_OK && s.state==BPL_EFFECT_RECORDED);
    assert(s.pending==F.ticket.sequence && F.ticket.lease.registry==F.lease.registry &&
        F.ticket.lease.slot==F.lease.slot && F.ticket.lease.generation==F.lease.generation);
    assert(F.pending_kind==kind && F.pending_resources==resources);
}
static uint32_t *word(unsigned region,uint32_t off)
{
    assert(!(off&3));
    if(region==APR_BCM_CONFIG){assert(off<sizeof F.config);return &F.config[off/4];}
    if(region==APR_BCM_BAR0){assert(off<sizeof F.bar0);return &F.bar0[off/4];}
    assert(region==APR_BCM_BAR2 && off>=BCT_RAM_BASE && off-BCT_RAM_BASE<sizeof F.bar2);
    return &F.bar2[(off-BCT_RAM_BASE)/4];
}
static int read_fake(void *ctx,unsigned region,uint32_t off,uint32_t *out)
{
    (void)ctx;
    if(++F.calls==F.fail_at){*out=UINT32_MAX;return -1;}
    if(region==APB_PHY){assert(!off);*out=F.phy;}
    else if(region==APB_PORT0){assert(off==0x804 || off==0x208);*out=off==0x804?F.run:F.link;}
    else{F.endpoint_reads++;*out=*word(region,off);}
    return 0;
}
static int write_fake(void *ctx,unsigned region,uint32_t off,uint32_t value)
{
    (void)ctx;
    assert(F.phy==0xf && F.run==1 && F.link==1);
    if(region==APR_BCM_CONFIG && off==0x80)pending(BPL_CONFIG,8);
    else if(region==APR_BCM_BAR2 && (off==DESC+4 || off==DESC+8 || off==DESC+12))pending(BPL_DESCRIPTOR,12);
    else if((region==APR_BCM_BAR2 && off==WI) ||
            (region==APR_BCM_BAR0 && (off==0x2140 || off==0x2a20)))pending(BPL_DOORBELL,12);
    else assert(!"unexpected writer");
    int failed=++F.calls==F.fail_at;F.writes++;
    if(!failed || F.apply_failed_write){
        if(region==APR_BCM_BAR0){
            assert(value==1 && F.pa_checked && F.map.mapped && F.ring.index_published==F.bells+1);
            assert((*word(APR_BCM_BAR2,WI)&0xffffu)==F.ring.write_index);
            assert(!memcmp(data+((F.ring.write_index+3)%4)*8,ITEM,8));F.bells++;
        }
        *word(region,off)=value;
        if(F.reset_on_index && region==APR_BCM_BAR2 && off==WI)F.phy=0x80;
        if(F.link_loss_on_index && region==APR_BCM_BAR2 && off==WI)F.link=0;
    }
    return failed?-1:0;
}
static int begin_effect(uint32_t kind,uint32_t resources)
{
    int rc=bpl_effect_begin(&F.registry,&F.lease,kind,resources,&F.ticket);
    if(!rc){F.pending_kind=kind;F.pending_resources=resources;pending(kind,resources);}
    return rc;
}
static int end_effect(int raw)
{
    uint32_t outcome=raw?BPL_REPORTED_FAILURE:BPL_REPORTED_SUCCESS;
    int detail=raw;
    if(F.force_unknown){outcome=BPL_UNKNOWN;detail=-777;}
    int journal=bpl_effect_result(&F.registry,&F.ticket,outcome,detail);
    assert(F.event_count<32);
    F.events[F.event_count++]=(struct event){F.ticket.sequence,F.pending_kind,F.pending_resources,raw,journal};
    F.last_raw=raw;F.last_journal=journal;F.pending_kind=F.pending_resources=0;
    return raw?raw:journal;
}
#define STEP(expr) do{int rc_=(expr);if(rc_)return rc_;}while(0)
#define EFFECT(kind,mask,expr) do{STEP(begin_effect((kind),(mask)));int raw_=(expr);STEP(end_effect(raw_));}while(0)
static void fresh(unsigned dar)
{
    memset(&F,0,sizeof F);memset(root,0xa5,sizeof root);memset(leaf,0x5a,sizeof leaf);memset(data,0xcc,sizeof data);
    F.phy=0xf;F.run=F.link=1;F.config[0]=0x442514e4;F.bar0[0]=0x10054378;
    *word(APR_BCM_BAR2,BCT_RAM_BASE+TP.ram_bytes-4)=SH;
    *word(APR_BCM_BAR2,SH)=0x02000007u|(dar?0x80000000u:0);
    *word(APR_BCM_BAR2,SH+40)=BCT_RAM_BASE+0x200;*word(APR_BCM_BAR2,SH+44)=BCT_RAM_BASE+0x204;
    *word(APR_BCM_BAR2,SH+48)=INFO;*word(APR_BCM_BAR2,INFO)=DESC;
    *word(APR_BCM_BAR2,INFO+4)=WI;*word(APR_BCM_BAR2,INFO+8)=RI;
    *word(APR_BCM_BAR2,WI)=0xface0000;*word(APR_BCM_BAR2,RI)=0xbeef0000;
    struct apb_io io={NULL,read_fake,write_fake,NULL,NULL,NULL};
    assert(apr_bind_bcm_fake(&F.accessor,&io,&F.io)==APB_OK);
    struct bpl_policy p={.platform_domain=1,.dart_domain=2,.irq_domain=3,.ram_count=1,.reserved_count=1,
        .ram={{UINT64_C(0x800000000),0x2000000}},.reserved={{UINT64_C(0x800080000),BPL_PAGE}}};
    assert(bpl_init(&F.registry,&p)==BPL_OK);
}
static int claim_bind(void)
{
    STEP(bpl_claim(&F.registry,&REQUEST,&F.lease));
    const struct bpl_span spans[3]={
        {BPL_ROOT,0,1,1,(uintptr_t)root,UINT64_C(0x800000000),BPL_PAGE,0},
        {BPL_LEAF,0,2,1,(uintptr_t)leaf,UINT64_C(0x800004000),BPL_PAGE,0},
        {BPL_DATA,BPL_READ,3,1,(uintptr_t)data,UINT64_C(0x800100000),BPL_PAGE,DLD_APCIE_BASE}};
    return bpl_bind_metadata(&F.registry,&F.lease,spans);
}
static int request_fake(void *ctx,const char *name,unsigned item,int required)
{
    (void)ctx;struct bpl_snapshot s;assert(snapshot(&s)==BPL_OK && !s.pending);
    F.fw_calls++;assert(item<BCI_ITEM_COUNT && required==(item==BCI_EXECUTABLE));
    assert(!strchr(name,'/') && strstr(name,"brcmfmac4378b3-pcie")==name);
    return item==BCI_EXECUTABLE && !strcmp(name,"brcmfmac4378b3-pcie.bin")?0:1;
}
/* Each memory/state-only entry checks its own live ticket before calling the
 * unchanged model; accessor assertions alone cannot observe these commits. */
static int owned_mapping(void)
{
    pending(BPL_MAPPING,7);
    return bfd_map_pages(&F.map,&F.workspace,DLD_APCIE_BASE,UINT64_C(0x800100000),DLD_PAGE,DLD_READ);
}
static int owned_irq_open(void)
{pending(BPL_INTERRUPT,16);return bmi_open(&F.irq,&F.io,&IP);}
static int owned_irq_arm(void)
{pending(BPL_INTERRUPT,16);return bmi_arm(&F.irq,1);}
static int owned_irq_mask(uint32_t vector,int masked)
{pending(BPL_INTERRUPT,16);return bmi_set_mask(&F.irq,vector,masked);}
static int owned_irq_receive(void)
{pending(BPL_INTERRUPT,16);return bmi_receive(&F.irq,IP.address,0);}
static int owned_irq_dispatch(uint32_t *vector,uint32_t *aic)
{pending(BPL_INTERRUPT,16);return bmi_dispatch_one(&F.irq,vector,aic);}
static int owned_irq_close(void)
{pending(BPL_INTERRUPT,16);return bmi_close(&F.irq);}
static int prepare(void)
{
    STEP(claim_bind());
    uint32_t phy=0;STEP(F.io.read32(F.io.context,APB_PHY,0,&phy));if((phy&0x8f)!=0xf)return -901;
    STEP(bct_init(&F.transport,&F.io,&TP));STEP(bct_identify(&F.transport));
    EFFECT(BPL_CONFIG,8,bct_select_window(&F.transport));
    STEP(bci_identify_fake(&F.io,&CP,&F.identity));assert(!F.identity.numeric_profile_verified);
    const char *q[]={"synthetic-test"};STEP(bci_build_fw_plan(&F.identity,q,1,&F.firmware));
    STEP(bci_request_fw_item(&F.io,&F.firmware,BCI_EXECUTABLE,request_fake,NULL,&F.request));
    int rc=bci_request_fw_item(&F.io,&F.firmware,BCI_BOARD_NVRAM,request_fake,NULL,&F.request);
    if(rc!=BCI_OPTIONAL_MISSING)return rc?rc:-903;
    struct bfd_storage storage={root,leaf,DLD_PAGE,DLD_PAGE,UINT64_C(0x800000000),UINT64_C(0x800004000)};
    STEP(bfd_init(&F.map,&F.io,&DP,&storage,0x100,3));
    EFFECT(BPL_MAPPING,7,owned_mapping());
    uint64_t pa=0;STEP(bfd_translate(&F.map,&F.workspace,0x100,3,DLD_APCIE_BASE,DLD_READ,&pa));
    assert(pa==UINT64_C(0x800100000));F.pa_checked=1;
    EFFECT(BPL_INTERRUPT,16,owned_irq_open());
    EFFECT(BPL_INTERRUPT,16,owned_irq_arm());
    EFFECT(BPL_INTERRUPT,16,owned_irq_mask(0,0));
    STEP(bct_discover_shared(&F.transport));
    struct bcr_plan plan={data,32,4,8,DLD_APCIE_BASE};STEP(bcr_open(&F.ring,&F.transport,&plan));
    EFFECT(BPL_DESCRIPTOR,12,bcr_configure(&F.ring));
    assert(*word(APR_BCM_BAR2,DESC+8)==DLD_APCIE_BASE);return 0;
}
static int finish(void)
{
    EFFECT(BPL_DOORBELL,12,bfi_submit(&F.ring,ITEM,8));
    /* Explicit host stimulus, not firmware progress or driver I/O. */
    *word(APR_BCM_BAR2,RI)=0xbeef0001;F.stimuli++;
    EFFECT(BPL_INTERRUPT,16,owned_irq_receive());
    uint32_t vector=UINT32_MAX,aic=UINT32_MAX;
    EFFECT(BPL_INTERRUPT,16,owned_irq_dispatch(&vector,&aic));assert(vector==0 && aic==704);
    EFFECT(BPL_DESCRIPTOR,12,bcr_close(&F.ring));
    EFFECT(BPL_INTERRUPT,16,owned_irq_mask(0,1));
    EFFECT(BPL_INTERRUPT,16,owned_irq_close());
    assert(F.map.mapped && F.ring.closed && F.irq.closed && F.stimuli==1);
    F.logical_complete=1;return 0;
}
static void healthy_and_conflicts(void)
{
    for(unsigned dar=0;dar<2;dar++){
        fresh(dar);assert(prepare()==0);unsigned calls=F.calls;
        assert(prepare()==BPL_CONFLICT && F.calls==calls);
        assert(finish()==0 && F.logical_complete && F.bells==1 && !memcmp(data,ITEM,8));
        struct bpl_snapshot s;assert(snapshot(&s)==BPL_OK && s.effect_count==HEALTHY_EFFECTS && !s.pending && s.ever_resources==31);
        assert(s.state==BPL_EFFECT_RECORDED && F.event_count==HEALTHY_EFFECTS && HEALTHY_EFFECTS<32);
        assert(bpl_retire_private(&F.registry,&F.lease)==BPL_RETAINED);
        for(unsigned n=0;n<3;n++){
            struct bpl_request q={n==0?0x100:0x101,n==1?3u:4u,n==2?1u:2u};struct bpl_lease out={0};
            assert(bpl_claim(&F.registry,&q,&out)==BPL_CONFLICT && !out.generation);
        }
        const uint32_t kinds[HEALTHY_EFFECTS]={BPL_CONFIG,BPL_MAPPING,BPL_INTERRUPT,BPL_INTERRUPT,BPL_INTERRUPT,
            BPL_DESCRIPTOR,BPL_DOORBELL,BPL_INTERRUPT,BPL_INTERRUPT,BPL_DESCRIPTOR,BPL_INTERRUPT,BPL_INTERRUPT};
        const uint32_t resources[HEALTHY_EFFECTS]={8,7,16,16,16,12,12,16,16,12,16,16};
        for(unsigned i=0;i<F.event_count;i++)assert(F.events[i].sequence==i+1 &&
            F.events[i].kind==kinds[i] && F.events[i].resources==resources[i] && !F.events[i].raw && !F.events[i].journal);
        assert(F.ring.bell_offset==(dar?0x2a20u:0x2140u));
    }
}
static void cold_private_and_unknown(void)
{
    fresh(0);F.phy=0x80;assert(prepare()==-901 && F.calls==1 && !F.writes && !F.endpoint_reads && !F.fw_calls);
    struct bpl_snapshot s;assert(snapshot(&s)==BPL_OK && !s.effect_count && s.state==BPL_PRIVATE_METADATA);
    assert(bpl_retire_private(&F.registry,&F.lease)==BPL_OK); /* metadata only, static storage retained */
    fresh(0);F.force_unknown=1;assert(prepare()==BPL_RETAINED && F.writes==1);
    assert(F.last_raw==0 && F.last_journal==BPL_RETAINED && !F.logical_complete);
    assert(snapshot(&s)==BPL_OK && s.state==BPL_QUARANTINED && !s.pending);
}
static void late_reset_and_retention(void)
{
    fresh(0);assert(prepare()==0);F.reset_on_index=1;
    assert(finish()==BCR_PHY_NOT_READY && !F.logical_complete && F.ring.faulted && F.ring.inflight && !F.bells);
    assert(*word(APR_BCM_BAR2,WI)==0xface0001 && !memcmp(data,ITEM,8));
    assert(F.last_raw==BCR_PHY_NOT_READY && F.last_journal==BPL_RETAINED);
    struct bpl_snapshot s;assert(snapshot(&s)==BPL_OK && s.state==BPL_QUARANTINED);
    assert(bpl_retire_private(&F.registry,&F.lease)==BPL_RETAINED);
    unsigned calls=F.calls;assert(begin_effect(BPL_DOORBELL,12)==BPL_STATE && F.calls==calls);
}
static void late_link_and_full(void)
{
    fresh(0);assert(prepare()==0);F.link_loss_on_index=1;
    assert(finish()==BCR_LINK_DOWN && !F.logical_complete && !F.bells && F.ring.inflight && F.ring.faulted);
    assert(F.last_raw==BCR_LINK_DOWN && F.last_journal==BPL_RETAINED);
    struct bpl_snapshot s;assert(snapshot(&s)==0 && s.state==BPL_QUARANTINED);
    assert(bpl_retire_private(&F.registry,&F.lease)==BPL_RETAINED);
    fresh(0);assert(prepare()==0);
    for(unsigned i=0;i<3;i++){
        assert(begin_effect(BPL_DOORBELL,12)==0);
        assert(end_effect(bfi_submit(&F.ring,ITEM,8))==0);
    }
    assert(F.bells==3);uint8_t before[sizeof data];memcpy(before,data,sizeof before);
    unsigned writes=F.writes;
    assert(begin_effect(BPL_DOORBELL,12)==0);
    assert(end_effect(bfi_submit(&F.ring,ITEM,8))==BCR_FULL);
    assert(F.last_raw==BCR_FULL && F.last_journal==BPL_RETAINED && F.writes==writes && !memcmp(data,before,sizeof data));
    assert(snapshot(&s)==0 && s.state==BPL_QUARANTINED && s.effect_count==10);
    unsigned calls=F.calls;assert(begin_effect(BPL_DOORBELL,12)==BPL_STATE && F.calls==calls);
}
static void exhaustion_and_tickets(void)
{
    fresh(0);assert(claim_bind()==0);
    for(unsigned i=0;i<32;i++){assert(begin_effect(BPL_CONFIG,8)==0);assert(end_effect(0)==0);}
    unsigned calls=F.calls;assert(begin_effect(BPL_CONFIG,8)==BPL_LIMIT && F.calls==calls);
    assert(bpl_retire_private(&F.registry,&F.lease)==BPL_RETAINED);
    fresh(0);assert(claim_bind()==0 && begin_effect(BPL_CONFIG,8)==0);
    struct bpl_ticket old=F.ticket;assert(end_effect(0)==0);
    assert(bpl_effect_result(&F.registry,&old,BPL_REPORTED_SUCCESS,0)==BPL_STALE);
    struct bpl_snapshot s;assert(snapshot(&s)==0 && s.state==BPL_QUARANTINED);
}
static void callback_faults(void)
{
    fresh(0);assert(prepare()==0 && finish()==0);unsigned total=F.calls;
    for(unsigned apply=0;apply<2;apply++)for(unsigned pos=1;pos<=total;pos++){
        fresh(0);F.fail_at=pos;F.apply_failed_write=apply;
        int raw=prepare();if(!raw)raw=finish();
        assert(raw && !F.logical_complete && F.calls==pos);
        struct bpl_snapshot s;assert(snapshot(&s)==0);
        if(F.last_raw){assert(F.last_journal==BPL_RETAINED && s.state==BPL_QUARANTINED && !s.pending);}
        if(s.effect_count)assert(bpl_retire_private(&F.registry,&F.lease)==BPL_RETAINED);
        struct bpl_lease duplicate={0};assert(bpl_claim(&F.registry,&REQUEST,&duplicate)==BPL_CONFLICT);
        assert(!duplicate.generation);
    }
    printf("HOST callback positions=%u before_after_cases=%u effects=%u\n",total,total*2,HEALTHY_EFFECTS);
}
#else
static int forbidden(void *ctx,unsigned r,uint32_t off,uint32_t *out)
{(void)ctx;(void)r;(void)off;(void)out;assert(!"default/kernel must not read");return -1;}
static void gated(void)
{
    struct apb_io io={NULL,forbidden,NULL,NULL,NULL,NULL};struct bct_session t={0};struct bfd_map m={0};
    struct bmi_source i={0};struct bcr_ring r={0};uint8_t out[8]={0x5a};
    assert(bct_init(&t,&io,NULL)==BCT_GATED && bfd_init(&m,&io,NULL,NULL,0,0)==BFD_GATED);
    assert(bmi_open(&i,&io,NULL)==BMI_GATED && bcr_open(&r,&t,NULL)==BCR_GATED);
    assert(bfi_encode(&r,out,8)==BFI_GATED && out[0]==0x5a);
}
#endif
int main(void)
{
    assert(!apb_shared_writes_compiled() && !apl_shared_writes_compiled());
    assert(!apg_fake_drive_compiled() && !ars_fake_enable_compiled());
#ifdef BCO_HOST
    healthy_and_conflicts();cold_private_and_unknown();late_reset_and_retention();late_link_and_full();exhaustion_and_tickets();callback_faults();
#else
    gated();
#endif
    puts("HOST ONLY PASS: actual c585 leases/tickets; synthetic host backing; NO native join or hardware proof");return 0;
}
