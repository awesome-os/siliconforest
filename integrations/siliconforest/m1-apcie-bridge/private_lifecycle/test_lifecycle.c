/* Original policy tests. Fresh fixtures are test isolation, not recovery. */
#include "lifecycle.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "lifecycle tests require assertions"
#endif
static unsigned checks;
#define CHECK(x) do { checks++; assert(x); } while (0)
static struct bpl_registry r, other, saved;
static struct bpl_policy policy(void)
{
    struct bpl_policy p={0};
    p.platform_domain=1; p.dart_domain=2; p.irq_domain=3;
    p.ram_count=1; p.reserved_count=1;
    p.ram[0]=(struct bpl_range){UINT64_C(0x800000000),UINT64_C(0x1000000)};
    p.reserved[0]=(struct bpl_range){UINT64_C(0x800f00000),BPL_PAGE};
    return p;
}
static void fresh(void)
{
    memset(&r,0,sizeof r); memset(&other,0,sizeof other);
    struct bpl_policy p=policy();
    CHECK(bpl_init(&r,&p)==BPL_OK); CHECK(bpl_init(&other,&p)==BPL_OK);
}
static struct bpl_request request(unsigned n)
{ return (struct bpl_request){0x100+n, n, 1u<<n}; }
static struct bpl_lease claim(unsigned n)
{
    struct bpl_request q=request(n); struct bpl_lease l={0};
    CHECK(bpl_claim(&r,&q,&l)==BPL_OK); return l;
}
static void spans(struct bpl_span s[3], unsigned n)
{
    for (unsigned i=0;i<3;i++) s[i]=(struct bpl_span){i+1,i==2?BPL_READ:0,
        n*3+i+1,1,UINT64_C(0x100000000)+n*0x100000+i*BPL_PAGE,
        UINT64_C(0x800000000)+n*0x100000+i*BPL_PAGE,BPL_PAGE,
        i==2?UINT64_C(0x100000)+n*0x100000:0};
}
static struct bpl_snapshot snapshot(const struct bpl_lease *l)
{
    struct bpl_snapshot s={0}; CHECK(bpl_inspect(&r,l,&s)==BPL_OK); return s;
}
static void unchanged(void) { CHECK(!memcmp(&r,&saved,sizeof r)); }
static void initialization(void)
{
    struct bpl_policy p=policy();
    fresh(); saved=r; CHECK(bpl_init(&r,&p)==BPL_STATE); unchanged();
    memset(&r,0,sizeof r); p.ram_count=0; saved=r;
    CHECK(bpl_init(&r,&p)==BPL_RANGE); unchanged();
    p=policy(); p.ram_count=9; CHECK(bpl_init(&r,&p)==BPL_RANGE); unchanged();
    p=policy(); p.ram[0].bytes=UINT64_MAX; CHECK(bpl_init(&r,&p)==BPL_RANGE); unchanged();
    p=policy(); p.ram_count=2;p.ram[1]=p.ram[0]; CHECK(bpl_init(&r,&p)==BPL_RANGE); unchanged();
    p=policy();p.reserved_count=2;p.reserved[1]=p.reserved[0];CHECK(bpl_init(&r,&p)==BPL_RANGE);unchanged();
    p=policy();p.dart_domain=0;CHECK(bpl_init(&r,&p)==BPL_RANGE);unchanged();
    CHECK(bpl_init(&r,&r.policy)==BPL_ARGUMENT);unchanged();
    CHECK(bpl_init(NULL,&p)==BPL_ARGUMENT);
}
static void ownership(void)
{
    fresh(); struct bpl_lease l=claim(0), out={0}, before=out;
    for (unsigned mode=0;mode<3;mode++) {
        struct bpl_request q=request(1);
        if(mode==0)q.rid=0x100; if(mode==1)q.sid=0; if(mode==2)q.vectors=1;
        saved=r;CHECK(bpl_claim(&r,&q,&out)==BPL_CONFLICT);unchanged();CHECK(!memcmp(&out,&before,sizeof out));
    }
    for(unsigned i=1;i<4;i++)(void)claim(i);
    struct bpl_request q=request(4);saved=r;CHECK(bpl_claim(&r,&q,&out)==BPL_LIMIT);unchanged();
    CHECK(bpl_retire_private(&r,&l)==BPL_OK);
    CHECK(bpl_retire_private(&r,&l)==BPL_STALE);
    struct bpl_lease newer=claim(0); CHECK(newer.generation!=l.generation && newer.slot==l.slot);
    saved=r;CHECK(bpl_quarantine(&r,&l,-44)==BPL_STALE);unchanged();
    struct bpl_lease bad=newer;bad.registry=&other;CHECK(bpl_quarantine(&r,&bad,-44)==BPL_STALE);unchanged();
    bad=newer;bad.generation++;CHECK(bpl_retire_private(&r,&bad)==BPL_STALE);unchanged();
    bad=newer;bad.slot=BPL_SLOTS;CHECK(bpl_retire_private(&r,&bad)==BPL_STALE);unchanged();
    struct bpl_snapshot s;memset(&s,0x5a,sizeof s);struct bpl_snapshot old=s;
    CHECK(bpl_inspect(&r,&bad,&s)==BPL_STALE);CHECK(!memcmp(&s,&old,sizeof s));
    CHECK(bpl_quarantine(&r,&newer,-44)==BPL_RETAINED);
    CHECK(bpl_quarantine(&r,&newer,-55)==BPL_RETAINED);
    CHECK(snapshot(&newer).first_error==-44);CHECK(bpl_retire_private(&r,&newer)==BPL_RETAINED);
    q=request(0);CHECK(bpl_claim(&r,&q,&out)==BPL_CONFLICT);
    fresh();r.next_generation=UINT64_MAX; /* Test injection; caller mutation forbidden. */
    l=claim(0);CHECK(l.generation==UINT64_MAX && r.exhausted);
    CHECK(bpl_retire_private(&r,&l)==BPL_OK);q=request(0);saved=r;
    CHECK(bpl_claim(&r,&q,&out)==BPL_LIMIT);unchanged();
    struct bpl_policy p=policy();CHECK(bpl_init(&r,&p)==BPL_STATE);unchanged();
    fresh();q=request(0);q.rid=0xff;CHECK(bpl_claim(&r,&q,&out)==BPL_RANGE);
    q=request(0);q.rid=0x10000;CHECK(bpl_claim(&r,&q,&out)==BPL_RANGE);
    q=request(0);q.sid=16;CHECK(bpl_claim(&r,&q,&out)==BPL_RANGE);
    q=request(0);q.vectors=256;CHECK(bpl_claim(&r,&q,&out)==BPL_RANGE);
    q=request(0);q.vectors=0;CHECK(bpl_claim(&r,&q,&out)==BPL_RANGE);
}
static void backing(void)
{
    struct bpl_span s[3];
    for(unsigned mode=0;mode<22;mode++) {
        fresh();struct bpl_lease l=claim(0);spans(s,0);
        switch(mode) {
        case 0:s[0].role=BPL_DATA;break;
        case 1:s[0].handle=0;break;
        case 2:s[0].generation=0;break;
        case 3:s[0].cpu=0;break;
        case 4:s[0].cpu=UINT64_MAX-0x3fff;break;
        case 5:s[0].cpu++;break;
        case 6:s[0].pa++;break;
        case 7:s[0].pa=UINT64_C(0x1000000000);break;
        case 8:s[0].bytes=0;break;
        case 9:s[0].bytes=BPL_PAGE*2;break;
        case 10:s[0].permissions=BPL_READ;break;
        case 11:s[0].iova=0x100000;break;
        case 12:s[2].permissions=0;break;
        case 13:s[2].permissions=4;break;
        case 14:s[2].iova=0xffffc000;break;
        case 15:s[2].iova=0xbff00000;break;
        case 16:s[2].iova=0xfc000;break;
        case 17:s[2].iova++;break;
        case 18:s[0].pa=UINT64_C(0x800f00000);break;
        case 19:s[0].pa=UINT64_C(0x900000000);break;
        case 20:s[0].cpu=s[1].cpu;break;
        case 21:s[0].pa=s[1].pa;break;
        }
        saved=r;CHECK(bpl_bind_metadata(&r,&l,s)!=BPL_OK);unchanged();
    }
    for(unsigned mode=0;mode<4;mode++) {
        fresh();struct bpl_lease a=claim(0),b=claim(1);spans(s,0);
        CHECK(bpl_bind_metadata(&r,&a,s)==BPL_OK);spans(s,1);
        const struct bpl_span *held=r.entries[a.slot].spans;
        if(mode==0)s[0].cpu=held[0].cpu;
        if(mode==1)s[0].pa=held[0].pa;
        if(mode==2)s[2].iova=held[2].iova;
        if(mode==3)s[0].handle=held[0].handle;
        saved=r;CHECK(bpl_bind_metadata(&r,&b,s)==BPL_CONFLICT);unchanged();
        CHECK(bpl_quarantine(&r,&a,-19)==BPL_RETAINED);
        saved=r;CHECK(bpl_bind_metadata(&r,&b,s)==BPL_CONFLICT);unchanged();
    }
    fresh();struct bpl_lease l=claim(0);spans(s,0);s[2].permissions=BPL_READ|BPL_WRITE;
    CHECK(bpl_bind_metadata(&r,&l,s)==BPL_OK);CHECK(snapshot(&l).state==BPL_PRIVATE_METADATA);
    s[0].pa=0;CHECK(r.entries[l.slot].spans[0].pa==UINT64_C(0x800000000));
    CHECK(bpl_bind_metadata(&r,&l,s)==BPL_STATE);
    CHECK(bpl_retire_private(&r,&l)==BPL_OK);l=claim(0);spans(s,0);
    CHECK(bpl_bind_metadata(&r,&l,s)==BPL_OK);
    fresh();l=claim(0);spans(s,0);s[0].cpu=(uint64_t)(uintptr_t)&r & ~(BPL_PAGE-1);
    CHECK(bpl_bind_metadata(&r,&l,s)==BPL_CONFLICT);
}
static void effects(void)
{
    for(uint32_t result=BPL_REPORTED_SUCCESS;result<=BPL_UNKNOWN;result++) {
        fresh();struct bpl_lease l=claim(0);struct bpl_ticket t={0},unchanged_ticket={0};
        CHECK(bpl_effect_begin(&r,&l,BPL_CONFIG,8,&t)==BPL_OK);
        struct bpl_snapshot s=snapshot(&l);
        CHECK(s.state==BPL_EFFECT_RECORDED && s.effect_count==1 && s.pending==1 && s.ever_resources==8);
        saved=r;CHECK(bpl_effect_begin(&r,&l,BPL_DESCRIPTOR,4,&unchanged_ticket)==BPL_STATE);unchanged();
        CHECK(!unchanged_ticket.sequence && !unchanged_ticket.lease.registry);
        CHECK(bpl_retire_private(&r,&l)==BPL_RETAINED);
        int rc=bpl_effect_result(&r,&t,result,result==BPL_REPORTED_SUCCESS?0:-88);
        CHECK(rc==(result==BPL_REPORTED_SUCCESS?BPL_OK:BPL_RETAINED));s=snapshot(&l);
        CHECK(s.effect_count==1 && s.ever_resources==8 && !s.pending);
        CHECK(r.entries[l.slot].effects[0].outcome==result);
        CHECK(bpl_retire_private(&r,&l)==BPL_RETAINED);
        struct bpl_request q=request(0);struct bpl_lease out={0};CHECK(bpl_claim(&r,&q,&out)==BPL_CONFLICT);
        if(result!=BPL_REPORTED_SUCCESS) {
            CHECK(s.state==BPL_QUARANTINED && s.first_error==-88);
            CHECK(bpl_effect_begin(&r,&l,BPL_CONFIG,8,&t)==BPL_STATE);
            CHECK(bpl_effect_result(&r,&t,BPL_REPORTED_SUCCESS,0)==BPL_RETAINED);
            CHECK(bpl_quarantine(&r,&l,-99)==BPL_RETAINED);CHECK(snapshot(&l).first_error==-88);
        }
    }
    fresh();struct bpl_lease l=claim(0);struct bpl_ticket t={0};
    CHECK(bpl_effect_begin(&r,&l,BPL_CONFIG,8,&t)==BPL_OK);
    struct bpl_ticket bad=t;bad.lease.registry=&other;saved=r;
    CHECK(bpl_effect_result(&r,&bad,BPL_REPORTED_SUCCESS,0)==BPL_STALE);unchanged();
    CHECK(bpl_effect_result(&r,&t,BPL_REPORTED_SUCCESS,-1)==BPL_ARGUMENT);unchanged();
    CHECK(bpl_effect_result(&r,&t,BPL_PENDING,0)==BPL_ARGUMENT);unchanged();
    bad=t;bad.sequence++;CHECK(bpl_effect_result(&r,&bad,BPL_REPORTED_SUCCESS,0)==BPL_STALE);
    CHECK(snapshot(&l).state==BPL_QUARANTINED && snapshot(&l).pending==1);
    fresh();l=claim(0);CHECK(bpl_effect_begin(&r,&l,BPL_CONFIG,8,&t)==BPL_OK);
    CHECK(bpl_effect_result(&r,&t,BPL_REPORTED_SUCCESS,0)==BPL_OK);
    CHECK(bpl_effect_result(&r,&t,BPL_REPORTED_SUCCESS,0)==BPL_STALE);CHECK(snapshot(&l).state==BPL_QUARANTINED);
    fresh();l=claim(0);
    for(unsigned i=0;i<BPL_EFFECTS;i++) {
        CHECK(bpl_effect_begin(&r,&l,BPL_INTERRUPT,16,&t)==BPL_OK);
        CHECK(t.sequence==i+1 && snapshot(&l).pending==i+1);
        CHECK(bpl_effect_result(&r,&t,BPL_REPORTED_SUCCESS,0)==BPL_OK);
    }
    saved=r;CHECK(bpl_effect_begin(&r,&l,BPL_CONFIG,8,&t)==BPL_LIMIT);unchanged();
    CHECK(bpl_retire_private(&r,&l)==BPL_RETAINED);
    fresh();l=claim(0);saved=r;
    CHECK(bpl_effect_begin(&r,&l,0,8,&t)==BPL_ARGUMENT);unchanged();
    CHECK(bpl_effect_begin(&r,&l,BPL_CONFIG,0,&t)==BPL_ARGUMENT);unchanged();
    CHECK(bpl_effect_begin(&r,&l,BPL_CONFIG,32,&t)==BPL_ARGUMENT);unchanged();
    CHECK(bpl_effect_begin(&r,&l,BPL_CONFIG,1,&t)==BPL_ARGUMENT);unchanged();
    CHECK(bpl_effect_begin(&r,&l,BPL_MAPPING,7,&t)==BPL_STATE);unchanged();
    CHECK(bpl_quarantine(&r,&l,0)==BPL_ARGUMENT);unchanged();
    struct bpl_span s[3];spans(s,0);CHECK(bpl_bind_metadata(&r,&l,s)==BPL_OK);
    CHECK(bpl_effect_begin(&r,&l,BPL_MAPPING,7,&t)==BPL_OK);
    CHECK(bpl_effect_result(&r,&t,BPL_REPORTED_SUCCESS,0)==BPL_OK);
    CHECK(bpl_effect_begin(&r,&l,BPL_DESCRIPTOR,12,&t)==BPL_OK);
    CHECK(snapshot(&l).ever_resources==15);CHECK(snapshot(&l).effect_count==2);
    CHECK(bpl_bind_metadata(&r,&l,s)==BPL_STATE);
    fresh();l=claim(0);struct bpl_request q=request(0);struct bpl_lease independent={0};
    CHECK(bpl_claim(&other,&q,&independent)==BPL_OK);
    /* Multiple registries are separate trusted domains, not physical proof. */
    CHECK(bpl_inspect(&r,&independent,&(struct bpl_snapshot){0})==BPL_STALE);
}
int main(void)
{
    initialization();ownership();backing();effects();
    printf("PRIVATE METADATA PASS checks=%u; no backing, activation or drain proof\n",checks);
    return 0;
}
