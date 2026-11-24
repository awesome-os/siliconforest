/* Original HOST-ONLY LEASED_FAKE_EFFECTS composition. Never native-backed. */
#include "lifecycle.h"
#include "bcm4378_transport.h"
#include "bcm4378_identity_fw.h"
#include "bcm4378_fake_dart.h"
#include "bcm4378_fake_irq.h"
#include "bcm4378_fake_ring.h"
#include "bcm4378_fake_item.h"
#include "linkup.h"
#include "perst_gpio.h"
#include "radio_enable.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "leased fixture requires assertions"
#endif
#if defined(LEASED_FAKE_EFFECTS) && defined(KERNEL)
#error "LEASED_FAKE_EFFECTS is host-only"
#endif
#if defined(APB_FAKE_REGISTER_FILE)
#error "leased fixture requires shared PHY and PMGR writes compiled out"
#endif
#if defined(LEASED_FAKE_EFFECTS) && (!defined(BCT_FAKE_BAR_FILE) || !defined(BCI_FAKE_DEVICE) || !defined(BFD_FAKE_DEVICE) || !defined(BMI_FAKE_DEVICE) || !defined(BCR_FAKE_DEVICE) || !defined(BFI_FAKE_DEVICE))
#error "leased fixture requires all explicit fake models"
#endif

#ifdef LEASED_FAKE_EFFECTS
#include "register_accessor.h"
enum { SH=BCT_RAM_BASE+0x100, INFO=BCT_RAM_BASE+0x300, DESC=BCT_RAM_BASE+0x400,
       WI=BCT_RAM_BASE+0x500, RI=BCT_RAM_BASE+0x504,
       PREP_EFFECTS=11, CHAIN_EFFECTS=18, UNKNOWN_RESULT=-970 };
_Static_assert(CHAIN_EFFECTS <= BPL_EFFECTS, "precomputed journal budget");
_Static_assert(BPL_PAGE == DLD_PAGE && BFI_SYNTHETIC_BYTES == 8, "frozen model sizes");
static const uint8_t ITEM[8]={0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88};
/* Reuse the already-pinned synthetic labels. None is an observed PA. */
static const uint64_t ROOT_PA=UINT64_C(0x800000000), LEAF_PA=UINT64_C(0x800004000);
static const uint64_t DATA_PA=UINT64_C(0x800100000);
static const struct bct_plan TP={0x10000,0x18000000,14,0xfeed0000};
static const struct bci_profile CP={0x18000000,0xffff,0xf0000,16,0xf0000000,28,0x4378,1};
static const struct dld_range RAM[]={{UINT64_C(0x800000000),0x2000000}};
static const struct dld_range RESERVED[]={{UINT64_C(0x800080000),DLD_PAGE}};
static const struct dld_policy DART_POLICY={{DLD_APCIE_BASE,DLD_APCIE_SIZE},RAM,1,RESERVED,1};
static const struct bmi_profile IRQ_PROFILE={UINT64_C(0xfffff000),0x100,0,8,704};
/* Exactly THREE backing objects, outside control storage. Scratch in F is
 * inactive codec work, not a fourth claimed/native backing allocation. */
_Alignas(16384) static uint8_t root_page[16384];
_Alignas(16384) static uint8_t leaf_page[16384];
_Alignas(16384) static uint8_t data_page[16384];
static struct fixture {
    struct bpl_registry registry; /* SOLE resource registry */
    struct bpl_lease lease;
    struct bpl_ticket ticket;
    struct bpl_span spans[BPL_SPANS];
    struct apr_accessor accessor;
    struct apb_io io;
    struct bct_session transport;
    struct bci_identity identity;
    struct bci_fw_plan firmware;
    struct bfd_map map;
    struct bfd_workspace workspace;
    struct bmi_source irq;
    struct bcr_ring ring;
    uint32_t config[0x1000/4], bar0[0x8000/4], bar2[0x10000/4];
    uint32_t phy, run, link;
    uint32_t kind, resources; /* operation trace, NEVER ownership authority */
    unsigned calls, writes, endpoint_reads, bells, begin_count, result_count;
    unsigned fail_at, apply_failed_write, reset_on_index, unknown_sequence;
    unsigned index_attempts, index_applied, bell_attempts, consumer_stimuli;
    unsigned chain_done;
    uint64_t translated_label;
    int32_t first_model_error, last_raw_result;
} F;

static struct bpl_snapshot inspect(void)
{
    struct bpl_snapshot s;
    assert(bpl_inspect(&F.registry,&F.lease,&s)==BPL_OK);
    return s;
}
static void exact_backing(void)
{
    struct bpl_snapshot s=inspect();
    const struct bpl_entry *e=&F.registry.entries[F.lease.slot];
    assert(F.lease.registry==&F.registry && e->generation==F.lease.generation);
    assert(e->request.rid==0x100 && e->request.sid==3 && e->request.vectors==1);
    assert(s.span_count==3 && !memcmp(e->spans,F.spans,sizeof F.spans));
    assert(e->spans[0].cpu==(uint64_t)(uintptr_t)root_page && e->spans[0].pa==ROOT_PA);
    assert(e->spans[1].cpu==(uint64_t)(uintptr_t)leaf_page && e->spans[1].pa==LEAF_PA);
    assert(e->spans[2].cpu==(uint64_t)(uintptr_t)data_page && e->spans[2].pa==DATA_PA);
    assert(e->spans[2].iova==DLD_APCIE_BASE && e->spans[2].permissions==DLD_READ);
    for (unsigned i=0;i<3;i++) assert(e->spans[i].bytes==BPL_PAGE &&
        e->spans[i].handle==i+1 && e->spans[i].generation==F.lease.generation);
    if (F.map.bound) assert(F.map.binding.rid==e->request.rid && F.map.binding.sid==e->request.sid);
    if (F.irq.bound) assert(F.irq.profile.rid==e->request.rid && !(F.irq.allocated&~e->request.vectors));
    if (F.ring.bound) assert(F.ring.plan.buffer==data_page && F.ring.plan.capacity==32 &&
                           F.ring.plan.fake_bus_label==e->spans[2].iova);
}
/* Callback authority is the real current core lease AND pending ticket.
 * Ordinary trace fields cannot fabricate, clear or extend the core owner. */
static void witness(uint32_t required)
{
    exact_backing();
    struct bpl_snapshot s=inspect();
    assert(s.state==BPL_PRIVATE_METADATA || s.state==BPL_EFFECT_RECORDED);
    if (F.kind) {
        assert(F.ticket.lease.registry==&F.registry && F.ticket.lease.slot==F.lease.slot &&
               F.ticket.lease.generation==F.lease.generation && s.pending==F.ticket.sequence && s.pending);
        const struct bpl_effect *e=&F.registry.entries[F.lease.slot].effects[s.pending-1];
        assert(e->sequence==F.ticket.sequence && e->kind==F.kind && e->outcome==BPL_PENDING);
        assert(e->resources==F.resources && (e->resources&required)==required);
    } else assert(!required && !s.pending);
}
static uint32_t *word(unsigned r,uint32_t off)
{
    assert(!(off&3u));
    if (r==APR_BCM_CONFIG) { assert(off<sizeof F.config); return &F.config[off/4]; }
    if (r==APR_BCM_BAR0) { assert(off<sizeof F.bar0); return &F.bar0[off/4]; }
    assert(r==APR_BCM_BAR2 && off>=BCT_RAM_BASE && off-BCT_RAM_BASE<sizeof F.bar2);
    return &F.bar2[(off-BCT_RAM_BASE)/4];
}
static int read_fake(void *ctx,unsigned r,uint32_t off,uint32_t *out)
{
    (void)ctx; witness(F.kind==BPL_DOORBELL?12:0);
    if (++F.calls==F.fail_at) return -1;
    if (r==APB_PHY) { assert(off==0); *out=F.phy; }
    else if (r==APB_PORT0) { assert(off==0x804 || off==0x208); *out=off==0x804?F.run:F.link; }
    else { F.endpoint_reads++; *out=*word(r,off); }
    return 0;
}
static int write_fake(void *ctx,unsigned r,uint32_t off,uint32_t value)
{
    (void)ctx;
    assert(F.phy==0xf && F.run==1 && F.link==1);
    if (r==APR_BCM_CONFIG && off==0x80) { assert(F.kind==BPL_CONFIG); witness(8); }
    else if (r==APR_BCM_BAR2 && (off==DESC+4 || off==DESC+8 || off==DESC+12)) {
        assert(F.kind==BPL_DESCRIPTOR); witness(12);
    } else {
        assert((r==APR_BCM_BAR2 && off==WI) || (r==APR_BCM_BAR0 && (off==0x2140 || off==0x2a20)));
        assert(F.kind==BPL_DOORBELL); witness(12);
    }
    if (r==APR_BCM_BAR2 && off==WI) F.index_attempts++;
    if (r==APR_BCM_BAR0) F.bell_attempts++;
    int fail=++F.calls==F.fail_at;
    F.writes++;
    if (!fail || F.apply_failed_write) {
        if (r==APR_BCM_BAR0) {
            assert(value==1 && F.translated_label==F.spans[2].pa && F.map.mapped);
            assert(F.ring.index_published==F.bells+1);
            assert((*word(APR_BCM_BAR2,WI)&0xffffu)==F.ring.write_index);
            assert(!memcmp(data_page+((F.ring.write_index+3)%4)*8,ITEM,8));
            F.bells++;
        }
        *word(r,off)=value;
        if (r==APR_BCM_BAR2 && off==WI) {
            F.index_applied++;
            if (F.reset_on_index) F.phy=0x80;
        }
    }
    return fail?-1:0;
}
static int input_failure(int rc)
{
    if (!rc) return 0;
    if (!F.first_model_error) F.first_model_error=rc;
    if (inspect().effect_count) assert(bpl_quarantine(&F.registry,&F.lease,rc)==BPL_RETAINED);
    return rc;
}
static int begin_effect(uint32_t kind,uint32_t resources)
{
    int rc=bpl_effect_begin(&F.registry,&F.lease,kind,resources,&F.ticket);
    if (rc) return rc;
    F.kind=kind; F.resources=resources; F.begin_count++;
    witness(0);
    return 0;
}
static int end_effect(int rc)
{
    F.last_raw_result=rc;
    uint32_t outcome=rc?BPL_REPORTED_FAILURE:BPL_REPORTED_SUCCESS;
    if (F.ticket.sequence==F.unknown_sequence) { rc=UNKNOWN_RESULT; outcome=BPL_UNKNOWN; }
    int core=bpl_effect_result(&F.registry,&F.ticket,outcome,rc);
    assert(core==(rc?BPL_RETAINED:BPL_OK));
    F.result_count++; F.kind=F.resources=0;
    if (rc && !F.first_model_error) F.first_model_error=rc;
    return rc;
}
#define READ(EXPR) do { int e_=(EXPR); if(e_) return input_failure(e_); } while(0)
#define MUTATE(K,M,EXPR) do { int e_=begin_effect((K),(M)); if(e_) return e_; e_=(EXPR); e_=end_effect(e_); if(e_) return e_; } while(0)

static void fresh_private(unsigned dar)
{
    /* Independent test-case construction only, never runtime recovery. */
    memset(&F,0,sizeof F); memset(root_page,0xa5,sizeof root_page);
    memset(leaf_page,0x5a,sizeof leaf_page); memset(data_page,0xcc,sizeof data_page);
    F.phy=0xf; F.run=F.link=1;
    F.config[0]=0x442514e4; F.bar0[0]=0x10054378; /* fabricated revision5 */
    *word(APR_BCM_BAR2,BCT_RAM_BASE+TP.ram_bytes-4)=SH;
    *word(APR_BCM_BAR2,SH)=0x02000007u|(dar?0x80000000u:0);
    *word(APR_BCM_BAR2,SH+40)=BCT_RAM_BASE+0x200;
    *word(APR_BCM_BAR2,SH+44)=BCT_RAM_BASE+0x204;
    *word(APR_BCM_BAR2,SH+48)=INFO;
    *word(APR_BCM_BAR2,INFO)=DESC; *word(APR_BCM_BAR2,INFO+4)=WI; *word(APR_BCM_BAR2,INFO+8)=RI;
    *word(APR_BCM_BAR2,WI)=0xface0000; *word(APR_BCM_BAR2,RI)=0xbeef0000;
    const struct bpl_policy p={1,2,3,1,1,{{UINT64_C(0x800000000),0x2000000}},{{UINT64_C(0x800080000),BPL_PAGE}}};
    const struct bpl_request q={0x100,3,1};
    assert(bpl_init(&F.registry,&p)==BPL_OK);
    assert(bpl_claim(&F.registry,&q,&F.lease)==BPL_OK);
    F.spans[0]=(struct bpl_span){BPL_ROOT,0,1,F.lease.generation,(uint64_t)(uintptr_t)root_page,ROOT_PA,BPL_PAGE,0};
    F.spans[1]=(struct bpl_span){BPL_LEAF,0,2,F.lease.generation,(uint64_t)(uintptr_t)leaf_page,LEAF_PA,BPL_PAGE,0};
    F.spans[2]=(struct bpl_span){BPL_DATA,BPL_READ,3,F.lease.generation,(uint64_t)(uintptr_t)data_page,DATA_PA,BPL_PAGE,DLD_APCIE_BASE};
    assert(bpl_bind_metadata(&F.registry,&F.lease,F.spans)==BPL_OK);
    exact_backing(); assert(inspect().state==BPL_PRIVATE_METADATA && !F.calls);
    struct apb_io raw={NULL,read_fake,write_fake,NULL,NULL,NULL};
    assert(apr_bind_bcm_fake(&F.accessor,&raw,&F.io)==APB_OK);
}
static int preflight(void)
{
    uint32_t phy=0,run=0,link=0;
    if (F.io.read32(F.io.context,APB_PHY,0,&phy)) return -900;
    if ((phy&0x8fu)!=0x0fu) return -901;
    if (F.io.read32(F.io.context,APB_PORT0,0x804,&run)) return -900;
    if (!(run&1)) return -902;
    if (F.io.read32(F.io.context,APB_PORT0,0x208,&link)) return -900;
    if ((link&0x45u)!=1) return -902;
    if (F.io.read32(F.io.context,APB_PHY,0,&phy)) return -900;
    return (phy&0x8fu)==0x0fu?0:-901;
}
static int leased_prepare(void)
{
    struct bpl_snapshot s;
    int rc=bpl_inspect(&F.registry,&F.lease,&s);
    if (rc) return rc;
    if (s.state!=BPL_PRIVATE_METADATA || s.pending || s.effect_count) return BPL_STATE;
    if (BPL_EFFECTS-s.effect_count<CHAIN_EFFECTS) return BPL_LIMIT;
    READ(preflight());
    MUTATE(BPL_CONFIG,8,bct_init(&F.transport,&F.io,&TP));
    MUTATE(BPL_CONFIG,8,bct_identify(&F.transport));
    MUTATE(BPL_CONFIG,8,bct_select_window(&F.transport));
    READ(bci_identify_fake(&F.io,&CP,&F.identity));
    assert(F.identity.valid && F.identity.revision==5 && !F.identity.numeric_profile_verified);
    const char *qualifiers[]={"synthetic-test"};
    READ(bci_build_fw_plan(&F.identity,qualifiers,1,&F.firmware)); /* names only */
    struct bfd_storage storage={root_page,leaf_page,DLD_PAGE,DLD_PAGE,ROOT_PA,LEAF_PA};
    MUTATE(BPL_MAPPING,7,bfd_init(&F.map,&F.io,&DART_POLICY,&storage,0x100,3));
    MUTATE(BPL_MAPPING,7,bfd_map_pages(&F.map,&F.workspace,DLD_APCIE_BASE,DATA_PA,DLD_PAGE,DLD_READ));
    READ(bfd_translate(&F.map,&F.workspace,0x100,3,DLD_APCIE_BASE,DLD_READ,&F.translated_label));
    assert(F.translated_label==F.spans[2].pa);
    MUTATE(BPL_INTERRUPT,16,bmi_open(&F.irq,&F.io,&IRQ_PROFILE));
    MUTATE(BPL_INTERRUPT,16,bmi_arm(&F.irq,1));
    MUTATE(BPL_INTERRUPT,16,bmi_set_mask(&F.irq,0,0));
    MUTATE(BPL_CONFIG,8,bct_discover_shared(&F.transport));
    struct bcr_plan plan={data_page,32,4,8,DLD_APCIE_BASE};
    MUTATE(BPL_DESCRIPTOR,12,bcr_open(&F.ring,&F.transport,&plan));
    MUTATE(BPL_DESCRIPTOR,12,bcr_configure(&F.ring));
    assert(inspect().effect_count==PREP_EFFECTS && F.begin_count==PREP_EFFECTS);
    return 0;
}
static int fake_consumer_progress(void)
{
    assert(F.kind==BPL_PRODUCER); witness(12);
    assert(F.ring.completed==1 && (*word(APR_BCM_BAR2,WI)&0xffff)==1);
    *word(APR_BCM_BAR2,RI)=0xbeef0001; F.consumer_stimuli++;
    return 0; /* explicit ordinary-RAM test stimulus, NOT firmware/DMA */
}
static int leased_finish(void)
{
    struct bpl_snapshot s=inspect();
    if (s.state!=BPL_EFFECT_RECORDED || s.pending || s.effect_count!=PREP_EFFECTS) return BPL_STATE;
    uint32_t vector=UINT32_MAX,aic=UINT32_MAX;
    READ(bfi_check(&F.ring,ITEM,sizeof ITEM));
    MUTATE(BPL_DOORBELL,12,bfi_submit(&F.ring,ITEM,sizeof ITEM));
    assert(F.bells==1 && F.ring.completed==1 && !memcmp(data_page,ITEM,8));
    MUTATE(BPL_PRODUCER,12,fake_consumer_progress());
    MUTATE(BPL_INTERRUPT,16,bmi_receive(&F.irq,IRQ_PROFILE.address,0));
    MUTATE(BPL_INTERRUPT,16,bmi_dispatch_one(&F.irq,&vector,&aic));
    assert(vector==0 && aic==704 && F.irq.delivered==1);
    MUTATE(BPL_DESCRIPTOR,12,bcr_close(&F.ring));
    MUTATE(BPL_INTERRUPT,16,bmi_set_mask(&F.irq,0,1));
    MUTATE(BPL_INTERRUPT,16,bmi_close(&F.irq));
    assert(F.ring.closed && F.irq.closed && F.map.mapped);
    assert(inspect().effect_count==CHAIN_EFFECTS && F.result_count==CHAIN_EFFECTS);
    assert(bpl_retire_private(&F.registry,&F.lease)==BPL_RETAINED);
    F.chain_done=1; /* observed logical close, NEVER ownership authority */
    return 0;
}
static void retained(int32_t first_error)
{
    struct bpl_snapshot s=inspect(); exact_backing();
    assert(s.state==(first_error?BPL_QUARANTINED:BPL_EFFECT_RECORDED));
    assert(s.first_error==first_error && bpl_retire_private(&F.registry,&F.lease)==BPL_RETAINED);
    const struct bpl_request conflicts[]={{0x100,4,2},{0x101,3,2},{0x101,4,1}};
    for (unsigned i=0;i<3;i++) {
        struct bpl_lease out={0}; unsigned calls=F.calls;
        assert(bpl_claim(&F.registry,&conflicts[i],&out)==BPL_CONFLICT && F.calls==calls);
        assert(!out.registry && !out.generation);
    }
}
static void healthy_and_exact_storage(void)
{
    assert(sizeof root_page==BPL_PAGE && sizeof leaf_page==BPL_PAGE && sizeof data_page==BPL_PAGE);
    assert(!((uintptr_t)root_page&(BPL_PAGE-1)) && !((uintptr_t)leaf_page&(BPL_PAGE-1)) && !((uintptr_t)data_page&(BPL_PAGE-1)));
    for (unsigned dar=0;dar<2;dar++) {
        fresh_private(dar); assert(leased_prepare()==0); retained(0);
        unsigned calls=F.calls;
        assert(leased_prepare()==BPL_STATE && F.calls==calls);
        assert(leased_finish()==0 && F.chain_done && F.consumer_stimuli==1);
        assert(F.ring.bell_offset==(dar?0x2a20u:0x2140u)); retained(0);
        calls=F.calls; assert(leased_finish()==BPL_STATE && F.calls==calls);
        for (unsigned i=32;i<sizeof data_page;i++) assert(data_page[i]==0xcc);
        assert(!memcmp(data_page,ITEM,8) && F.map.mapped_pages==1 && F.index_applied==1);
        assert(F.begin_count==F.result_count && !inspect().pending);
        unsigned calls_before=F.calls;
        assert(bpl_bind_metadata(&F.registry,&F.lease,F.spans)==BPL_STATE && F.calls==calls_before);
    }
}
static void private_failures_and_conflicts(void)
{
    for (unsigned bad=0;bad<7;bad++) {
        fresh_private(0);
        if (bad==0) F.phy=0x80; else if (bad==1) F.phy=0x8f;
        else if (bad==2) F.phy=0xb; else if (bad==3) F.run=0;
        else if (bad==4) F.link=0; else if (bad==5) F.link=5; else F.link=0x41;
        assert(leased_prepare()!=0 && !F.writes && !F.endpoint_reads && !F.begin_count);
        assert(inspect().state==BPL_PRIVATE_METADATA && !inspect().effect_count);
        for (unsigned i=0;i<BPL_PAGE;i++) assert(root_page[i]==0xa5 && leaf_page[i]==0x5a && data_page[i]==0xcc);
        assert(bpl_retire_private(&F.registry,&F.lease)==BPL_OK); /* metadata only, no storage free */
    }
    fresh_private(0); unsigned calls=F.calls;
    const struct bpl_request q[]={{0x100,4,2},{0x101,3,2},{0x101,4,1}};
    for (unsigned i=0;i<3;i++) { struct bpl_lease out={0}; assert(bpl_claim(&F.registry,&q[i],&out)==BPL_CONFLICT); }
    assert(F.calls==calls);
    struct bpl_lease old=F.lease;
    assert(bpl_retire_private(&F.registry,&old)==BPL_OK);
    const struct bpl_request same={0x100,3,1};
    assert(bpl_claim(&F.registry,&same,&F.lease)==BPL_OK && F.lease.generation!=old.generation);
    for (unsigned i=0;i<3;i++) F.spans[i].generation=F.lease.generation;
    assert(bpl_bind_metadata(&F.registry,&F.lease,F.spans)==BPL_OK);
    struct bpl_snapshot snap={0}; struct bpl_ticket t={0};
    assert(bpl_inspect(&F.registry,&old,&snap)==BPL_STALE);
    assert(bpl_effect_begin(&F.registry,&old,BPL_CONFIG,8,&t)==BPL_STALE);
    struct bpl_lease wrong=F.lease; wrong.registry=NULL;
    assert(bpl_effect_begin(&F.registry,&wrong,BPL_CONFIG,8,&t)==BPL_STALE && F.calls==calls);
    exact_backing();
}
static void partial_pending_and_unknown(void)
{
    fresh_private(0); assert(leased_prepare()==0); F.reset_on_index=1;
    assert(leased_finish()==BCR_PHY_NOT_READY);
    assert(F.ring.faulted && F.ring.dirty && F.ring.inflight && F.index_applied==1 && !F.bells);
    assert(F.ring.index_write_attempts==1 && F.ring.index_published==0 && !F.ring.bell_attempts);
    assert(*word(APR_BCM_BAR2,WI)==0xface0001 && !memcmp(data_page,ITEM,8));
    retained(BCR_PHY_NOT_READY);
    unsigned calls=F.calls; assert(leased_prepare()==BPL_STATE && leased_finish()==BPL_STATE && F.calls==calls);

    fresh_private(0); assert(leased_prepare()==0);
    assert(begin_effect(BPL_INTERRUPT,16)==0); assert(end_effect(bmi_receive(&F.irq,IRQ_PROFILE.address,0))==0);
    assert(begin_effect(BPL_INTERRUPT,16)==0); assert(end_effect(bmi_set_mask(&F.irq,0,1))==0);
    assert(begin_effect(BPL_INTERRUPT,16)==0); assert(end_effect(bmi_close(&F.irq))==BMI_BUSY);
    assert(F.irq.pending==1 && !F.irq.closed); retained(BMI_BUSY);
    for (unsigned seq=1;seq<=CHAIN_EFFECTS;seq++) {
        fresh_private(0); F.unknown_sequence=seq;
        int e=leased_prepare(); if (!e) e=leased_finish();
        assert(e==UNKNOWN_RESULT && !F.chain_done && F.begin_count==seq && F.result_count==seq);
        struct bpl_snapshot s=inspect();
        assert(!s.pending && s.effect_count==seq && F.registry.entries[F.lease.slot].effects[seq-1].outcome==BPL_UNKNOWN);
        retained(UNKNOWN_RESULT);
    }
    fresh_private(0); assert(leased_prepare()==0);
    unsigned before=F.calls;
    assert(bpl_effect_result(&F.registry,&F.ticket,BPL_REPORTED_SUCCESS,0)==BPL_STALE);
    assert(F.calls==before); retained(BPL_STALE); /* stale completed ticket cannot bless a new effect */
    fresh_private(0); assert(leased_prepare()==0);
    for (unsigned seq=PREP_EFFECTS;seq<BPL_EFFECTS;seq++) {
        assert(begin_effect(BPL_INTERRUPT,16)==0); assert(end_effect(0)==0); /* explicit no-op potential effect */
    }
    before=F.calls; assert(begin_effect(BPL_INTERRUPT,16)==BPL_LIMIT && F.calls==before);
    retained(0);
}
static void all_callback_faults(void)
{
    fresh_private(0); assert(leased_prepare()==0 && leased_finish()==0);
    unsigned total=F.calls, journaled=0, private_refusals=0;
    for (unsigned applied=0;applied<2;applied++) for (unsigned n=1;n<=total;n++) {
        fresh_private(0); F.fail_at=n; F.apply_failed_write=applied;
        int e=leased_prepare(); if (!e) e=leased_finish();
        assert(e && !F.chain_done && F.calls==n && F.first_model_error==e);
        struct bpl_snapshot s=inspect();
        if (s.effect_count) { retained(e); journaled++; }
        else { assert(s.state==BPL_PRIVATE_METADATA); assert(bpl_retire_private(&F.registry,&F.lease)==BPL_OK); private_refusals++; }
        for (unsigned i=32;i<sizeof data_page;i++) assert(data_page[i]==0xcc);
    }
    printf("LEASED_FAKE_EFFECTS callbacks=%u fault_cases=%u quarantined=%u private_refusals=%u effects=%u\n",
           total,total*2,journaled,private_refusals,CHAIN_EFFECTS);
}
#else
enum { LEASED_GATED=100 };
static int leased_prepare(void) { return LEASED_GATED; }
static int leased_finish(void) { return LEASED_GATED; }
static void gated(void)
{
    assert(leased_prepare()==LEASED_GATED && leased_finish()==LEASED_GATED);
    assert(!bct_fake_transport_compiled());
    struct bci_identity id={0};
    assert(bci_identify_fake(NULL,NULL,&id)==BCI_GATED);
    assert(!bfd_fake_map_compiled() && !bmi_fake_source_compiled() && !bcr_fake_ring_compiled() && !bfi_fake_item_compiled());
}
#endif
int main(void)
{
    assert(!apb_shared_writes_compiled() && !apl_shared_writes_compiled());
    assert(!apg_fake_drive_compiled() && !ars_fake_enable_compiled());
#ifdef LEASED_FAKE_EFFECTS
    healthy_and_exact_storage(); private_failures_and_conflicts(); partial_pending_and_unknown(); all_callback_faults();
#else
    gated();
#endif
    puts("HOST ONLY PASS: LEASED_FAKE_EFFECTS real c585 lease/ticket, synthetic backing retained; no native join");
    return 0;
}
