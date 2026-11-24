#define _POSIX_C_SOURCE 200809L
#include "owned.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#error "backing tests require assertions"
#endif
static atomic_uint checks;
#define CHECK(x) do {atomic_fetch_add(&checks,1);assert(x);}while(0)
enum fault { NONE, BEFORE, AFTER, UNKNOWN, STALE, NULL_ALLOC };
struct object { void *page;unsigned refs,wire,allocations,prepares,completes,resets;uint64_t pa; };
struct fixture {
    struct bno_manager manager;
    struct object objects[4][3], *refs[4][3];
    pthread_mutex_t mutex,wait_mutex;
    pthread_cond_t cond;
    atomic_int held;
    unsigned calls[8],trace[256],trace_count,zeroed,copied;
    unsigned fail_action,fail_role,fault,bad_view,reentry,block,waiting,unblock;
};
static struct fixture *f;
static void (*fixture_dispatch)(void *,const struct bno_effect_call *,struct bno_effect_return *);
static struct bno_ops caller_boot_ops;
static int top_policy;
static void (*fixture_query_hook)(void);
static void take(void *ctx) {struct fixture *x=ctx;CHECK(!pthread_mutex_lock(&x->mutex));atomic_store(&x->held,1);}
static void drop(void *ctx) {struct fixture *x=ctx;atomic_store(&x->held,0);CHECK(!pthread_mutex_unlock(&x->mutex));}
static unsigned total_refs(void)
{unsigned n=0;for(unsigned a=0;a<4;a++)for(unsigned b=0;b<3;b++)n+=f->objects[a][b].refs;return n;}
static void invoke(void *ctx,const struct bno_call *c,struct bno_return *r)
{
    struct fixture *x=ctx;CHECK(!atomic_load(&x->held));
    CHECK(c->slot<4 && c->role<3 && c->action<=BNO_RESET_REF);
    x->calls[c->action]++;CHECK(x->trace_count<256);x->trace[x->trace_count++]=c->action*10+c->role;
    struct bno_snapshot snap;CHECK(bno_inspect(&x->manager,c->slot,&snap)==0);
    CHECK(snap.busy && snap.call_pending && snap.sequence==c->sequence);
    if(x->reentry) {
        unsigned before=x->trace_count;struct bno_request q={0x111,9,128};struct bno_token t={0};
        CHECK(bno_acquire(&x->manager,&q,0x900000,BPL_READ,&t)==BNO_BUSY);
        CHECK(x->trace_count==before);
        struct bno_token live={&x->manager,c->generation,c->slot};
        CHECK(bno_retire_private(&x->manager,&live)==BNO_BUSY);
    }
    if(x->block) {
        CHECK(!pthread_mutex_lock(&x->wait_mutex));x->waiting=1;
        CHECK(!pthread_cond_broadcast(&x->cond));
        while(!x->unblock)CHECK(!pthread_cond_wait(&x->cond,&x->wait_mutex));
        CHECK(!pthread_mutex_unlock(&x->wait_mutex));x->block=0;
    }
    struct object *o=x->refs[c->slot][c->role];
    *r=(struct bno_return){c->sequence,c->generation,(uint64_t)(uintptr_t)o,
        c->slot,c->role,c->action,BNO_CALL_OK,0,{0}};
    unsigned mode=(x->fail_action==c->action && x->fail_role==c->role)?x->fault:NONE;
    if(mode==NULL_ALLOC) {CHECK(c->action==BNO_ALLOCATE);r->outcome=BNO_CALL_REFUSED;return;}
    if(mode==BEFORE || mode==UNKNOWN) {r->outcome=mode==UNKNOWN?BNO_CALL_UNKNOWN:BNO_CALL_ERROR;r->native_error=-700;return;}
    switch(c->action) {
    case BNO_ALLOCATE:
        CHECK(!o);o=&x->objects[c->slot][c->role];CHECK(!o->refs && !o->page);
        CHECK(!posix_memalign(&o->page,BPL_PAGE,BPL_PAGE));memset(o->page,0xa7,BPL_PAGE);
        o->refs=1;o->wire=1;o->allocations++; /* constructor baseline */
        o->pa=(top_policy?UINT64_C(0x1000000000)-3*BPL_PAGE:UINT64_C(0x800000000))+c->slot*0x100000+c->role*BPL_PAGE;
        x->refs[c->slot][c->role]=o; /* actual fake strong anchor BEFORE return */
        r->identity=(uint64_t)(uintptr_t)o;break;
    case BNO_PREPARE:
        for(unsigned i=0;i<3;i++)CHECK(x->refs[c->slot][i] && x->refs[c->slot][i]->refs==1);
        CHECK(o && o->refs==1 && o->wire==1);o->wire++;o->prepares++;break;
    case BNO_QUERY:
        if(fixture_query_hook)fixture_query_hook();
        CHECK(o && o->refs==1 && o->wire==2);
        r->view=(struct bno_view){(uint64_t)(uintptr_t)o->page,o->pa,BPL_PAGE,BPL_PAGE,BPL_PAGE};
        if(c->role==2)switch(x->bad_view) {
        case 1:r->view.capacity--;break;case 2:r->view.length--;break;
        case 3:r->view.span=0;break;case 4:r->view.span=BPL_PAGE*2;break;
        case 5:r->view.cpu++;break;case 6:r->view.pa++;break;
        case 7:r->view.pa=UINT64_C(0x1000000000);break;
        case 8:r->view.pa=UINT64_C(0x800f00000);break; /* protected */
        case 9:r->view.pa=UINT64_C(0x800e00000);break; /* reserved */
        case 10:r->view.cpu=(uint64_t)(uintptr_t)x & ~(BPL_PAGE-1);break;
        case 11:r->view.cpu=(uint64_t)(uintptr_t)x->refs[c->slot][0]->page;break;
        case 12:r->view.pa=x->refs[c->slot][0]->pa;break;
        case 13:r->identity++;break;case 14:r->view.cpu=0;break;
        case 15:r->view.pa=UINT64_C(0x700000000);break;
        case 16:r->view.cpu=UINT64_MAX-0x3fff;break;
        case 17:r->view.cpu=0x10000000;break; /* protected CPU */
        case 18:r->view.cpu=(uint64_t)(uintptr_t)x->refs[0][0]->page;break;
        case 19:r->view.pa=x->refs[0][0]->pa;break;
        }
        break;
    case BNO_ZERO:
    case BNO_COPY_DATA: {
        CHECK(o && o->refs==1 && o->wire==2);
        struct bno_job *j=&x->manager.jobs[c->slot];struct bpl_snapshot core;
        CHECK(j->metadata_bound && bpl_inspect(&x->manager.registry,&j->lease,&core)==0);
        CHECK(core.state==BPL_PRIVATE_METADATA && !core.ever_resources);
        for(unsigned i=0;i<3;i++)CHECK(j->records[i].owns_pair && x->refs[c->slot][i]->wire==2);
        if(c->action==BNO_ZERO){memset(o->page,0,BPL_PAGE);x->zeroed++;}
        else {CHECK(c->role==2 && c->offset+c->bytes<=BPL_PAGE);memcpy((uint8_t*)o->page+c->offset,c->source,c->bytes);x->copied++;}
        break;
    }
    case BNO_COMPLETE: {
        struct bno_job *j=&x->manager.jobs[c->slot];struct bpl_snapshot core;
        CHECK(bpl_inspect(&x->manager.registry,&j->lease,&core)==0);CHECK(!j->core_retired);
        CHECK(o && o->refs==1 && o->wire==2);o->wire--;o->completes++;break;
    }
    case BNO_RESET_REF: {
        struct bno_job *j=&x->manager.jobs[c->slot];struct bpl_snapshot core;
        CHECK(j->core_retired && bpl_inspect(&x->manager.registry,&j->lease,&core)==BPL_STALE);
        for(unsigned i=0;i<3;i++)CHECK(!j->records[i].owns_pair || j->records[i].pair_balanced);
        CHECK(o && o->refs==1 && o->wire==1);o->refs--;o->wire--;o->resets++;
        free(o->page);o->page=NULL;x->refs[c->slot][c->role]=NULL;r->identity=0;break;
    }
    default:CHECK(0);
    }
    if(mode==AFTER){r->outcome=BNO_CALL_ERROR;r->native_error=-701;}
    if(mode==STALE)r->sequence++;
    if(x->bad_view==20 && c->action==BNO_ALLOCATE && c->role==2)
        r->identity=(uint64_t)(uintptr_t)x->refs[c->slot][0];
}
static struct bpl_policy policy(void)
{
    struct bpl_policy p={0};p.platform_domain=1;p.dart_domain=2;p.irq_domain=3;p.ram_count=p.reserved_count=1;
    p.ram[0]=(struct bpl_range){top_policy?0:UINT64_C(0x800000000),top_policy?UINT64_C(0x1000000000):0x1000000};
    p.reserved[0]=(struct bpl_range){UINT64_C(0x800e00000),BPL_PAGE};return p;
}
static struct bno_protection protection(void)
{
    struct bno_protection p={0};p.provenance=123;p.complete=p.pa_count=p.cpu_count=1;
    p.pa[0]=(struct bpl_range){UINT64_C(0x800f00000),BPL_PAGE};
    p.cpu[0]=(struct bpl_range){0x10000000,BPL_PAGE};return p;
}
static void dispose(void)
{
    if(!f)return;
    /* Host test isolation ONLY, not a provider recovery/destruction path. */
    for(unsigned a=0;a<4;a++)for(unsigned b=0;b<3;b++)free(f->objects[a][b].page);
    CHECK(!pthread_mutex_destroy(&f->mutex));CHECK(!pthread_mutex_destroy(&f->wait_mutex));
    CHECK(!pthread_cond_destroy(&f->cond));free(f);f=NULL;
}
static void fresh(void)
{
    dispose();f=calloc(1,sizeof *f);CHECK(f);
    CHECK(!pthread_mutex_init(&f->mutex,NULL));CHECK(!pthread_mutex_init(&f->wait_mutex,NULL));CHECK(!pthread_cond_init(&f->cond,NULL));
    struct bno_ops o={f,take,drop,invoke,fixture_dispatch,fixture_dispatch!=NULL};struct bpl_policy p=policy();struct bno_protection pro=protection();
    caller_boot_ops=o;
    CHECK(bno_init(&f->manager,&caller_boot_ops,&p,&pro,BPL_PAGE,(struct bpl_range){(uint64_t)(uintptr_t)f,sizeof *f})==0);
}
static int acquire(unsigned n,struct bno_token *t)
{struct bno_request q={0x100+n,n,1u<<n};return bno_acquire(&f->manager,&q,0x100000+n*0x100000,BPL_READ,t);}
static struct bno_snapshot inspect(unsigned slot)
{struct bno_snapshot s={0};CHECK(bno_inspect(&f->manager,slot,&s)==0);return s;}
static void healthy(void)
{
    fresh();f->reentry=1;struct bno_token t={0};CHECK(acquire(0,&t)==0);
    CHECK(total_refs()==3 && f->zeroed==0 && inspect(0).phase==BNO_VALIDATED && !inspect(0).call_pending);
    for(unsigned i=0;i<3;i++)CHECK(((uint8_t*)f->refs[0][i]->page)[0]==0xa7);
    CHECK(bno_zero_private(&f->manager,&t)==0);CHECK(f->zeroed==3);
    const uint8_t data[]={1,2,3,4};CHECK(bno_copy_private_data(&f->manager,&t,BPL_PAGE-4,data,4)==0);
    CHECK(!memcmp((uint8_t*)f->refs[0][2]->page+BPL_PAGE-4,data,4));
    CHECK(bno_copy_private_data(&f->manager,&t,BPL_PAGE-3,data,4)==BNO_ARGUMENT);
    CHECK(bno_copy_private_data(&f->manager,&t,0,f->refs[0][0]->page,4)==BNO_ARGUMENT);
    CHECK(bno_retire_private(&f->manager,&t)==0);CHECK(total_refs()==0 && inspect(0).phase==BNO_REFS_RELEASED);
    CHECK(f->calls[BNO_COMPLETE]==3 && f->calls[BNO_RESET_REF]==3);
    CHECK(bno_retire_private(&f->manager,&t)==BNO_STALE);
    struct bno_token next={0};CHECK(acquire(0,&next)==0);CHECK(next.generation!=t.generation);
    CHECK(bno_zero_private(&f->manager,&t)==BNO_STALE);
    /* Dropping a facade/token is not a reference release. */
    memset(&next,0,sizeof next);CHECK(total_refs()==3 && inspect(0).phase==BNO_VALIDATED);
}
static void allocation_and_prepare_faults(void)
{
    for(unsigned i=0;i<3;i++) {
        fresh();f->fail_action=BNO_ALLOCATE;f->fail_role=i;f->fault=NULL_ALLOC;
        struct bno_token t={0},before=t;CHECK(acquire(0,&t)==BNO_REFUSED);CHECK(!memcmp(&t,&before,sizeof t));
        CHECK(total_refs()==0 && !f->calls[BNO_PREPARE] && !f->calls[BNO_COMPLETE]);
        CHECK(f->calls[BNO_RESET_REF]==i);
    }
    for(unsigned action=BNO_ALLOCATE;action<=BNO_PREPARE;action++)for(unsigned i=0;i<3;i++)
    for(unsigned mode=BEFORE;mode<=STALE;mode++) {
        fresh();f->fail_action=action;f->fail_role=i;f->fault=mode;
        struct bno_token t={0};CHECK(acquire(0,&t)==BNO_RETAINED);
        CHECK(!f->calls[BNO_COMPLETE] && !f->calls[BNO_RESET_REF]);
        unsigned expected=action==BNO_PREPARE?3:i+(mode==AFTER || mode==STALE);
        CHECK(total_refs()==expected);CHECK(inspect(0).phase==BNO_QUARANTINED);
        unsigned calls=f->trace_count;CHECK(inspect(0).halted);CHECK(acquire(0,&t)==BNO_BUSY);CHECK(calls==f->trace_count);
    }
}
static void validation_and_retirement_faults(void)
{
    for(unsigned mode=1;mode<=17;mode++) {
        fresh();f->bad_view=mode;struct bno_token t={0};int rc=acquire(0,&t);
        CHECK(rc==(mode==13?BNO_RETAINED:BNO_VALIDATION));CHECK(!f->zeroed);
        CHECK(total_refs()==(mode==13?3:0));
        CHECK(f->calls[BNO_COMPLETE]==(mode==13?0:3));
    }
    for(unsigned i=0;i<3;i++)for(unsigned mode=BEFORE;mode<=STALE;mode++) {
        fresh();struct bno_token t={0};CHECK(acquire(0,&t)==0);
        f->fail_action=BNO_COMPLETE;f->fail_role=i;f->fault=mode;
        CHECK(bno_retire_private(&f->manager,&t)==BNO_RETAINED);
        CHECK(total_refs()==3 && !f->calls[BNO_RESET_REF]);
        CHECK(f->calls[BNO_COMPLETE]==3-i);
        unsigned calls=f->trace_count;CHECK(bno_retire_private(&f->manager,&t)==BNO_BUSY);CHECK(f->trace_count==calls);
        struct bpl_snapshot s;CHECK(bpl_inspect(&f->manager.registry,&f->manager.jobs[0].lease,&s)==0);CHECK(s.state==BPL_QUARANTINED);
    }
    for(unsigned i=0;i<3;i++)for(unsigned mode=BEFORE;mode<=STALE;mode++) {
        fresh();struct bno_token t={0};CHECK(acquire(0,&t)==0);
        f->fail_action=BNO_RESET_REF;f->fail_role=i;f->fault=mode;
        CHECK(bno_retire_private(&f->manager,&t)==BNO_RETAINED);CHECK(inspect(0).halted);
        CHECK(f->calls[BNO_COMPLETE]==3 && f->calls[BNO_RESET_REF]==3-i);
        CHECK(total_refs()==i+((mode==BEFORE || mode==UNKNOWN)?1:0));
        unsigned calls=f->trace_count;struct bno_token other={0};CHECK(acquire(1,&other)==BNO_BUSY);CHECK(f->trace_count==calls);
    }
}
static void remaining_boundaries(void)
{
    for(unsigned action=BNO_QUERY;action<=BNO_COPY_DATA;action++)for(unsigned mode=BEFORE;mode<=STALE;mode++) {
        fresh();struct bno_token t={0};
        if(action==BNO_QUERY) {f->fail_action=action;f->fail_role=1;f->fault=mode;
            int rc=acquire(0,&t);CHECK(rc==((mode==BEFORE || mode==AFTER)?BNO_VALIDATION:BNO_RETAINED));
        } else {
            CHECK(acquire(0,&t)==0);if(action==BNO_COPY_DATA)CHECK(bno_zero_private(&f->manager,&t)==0);
            f->fail_action=action;f->fail_role=action==BNO_ZERO?1:2;f->fault=mode;
            const uint8_t b=9;
            CHECK((action==BNO_ZERO?bno_zero_private(&f->manager,&t):bno_copy_private_data(&f->manager,&t,0,&b,1))==BNO_RETAINED);
            CHECK(total_refs()==3 && !f->calls[BNO_COMPLETE] && !f->calls[BNO_RESET_REF]);
        }
    }
    fresh();struct bno_token t[4];for(unsigned i=0;i<4;i++)CHECK(acquire(i,&t[i])==0);
    CHECK(total_refs()==12);struct bno_token extra={0};CHECK(acquire(4,&extra)==BPL_LIMIT);
    f->fail_action=BNO_ZERO;f->fail_role=0;f->fault=BEFORE;
    CHECK(bno_zero_private(&f->manager,&t[0])==BNO_RETAINED);CHECK(inspect(0).halted);
    for(unsigned i=1;i<4;i++)CHECK(bno_zero_private(&f->manager,&t[i])==BNO_BUSY);
    CHECK(total_refs()==12 && acquire(4,&extra)==BNO_BUSY && !f->calls[BNO_RESET_REF]);
    fresh();CHECK(acquire(0,&t[0])==0);
    struct bpl_ticket effect;CHECK(bpl_effect_begin(&f->manager.registry,&f->manager.jobs[0].lease,BPL_CONFIG,8,&effect)==0);
    unsigned calls=f->trace_count;CHECK(bno_retire_private(&f->manager,&t[0])==BNO_RETAINED);CHECK(f->trace_count==calls && total_refs()==3);
    fresh();f->manager.next_handle=UINT64_MAX-2;CHECK(acquire(0,&t[0])==BNO_LIMIT);CHECK(!f->trace_count);
    fresh();f->manager.next_sequence=UINT64_MAX;CHECK(acquire(0,&t[0])==BNO_LIMIT);CHECK(!f->trace_count);
    fresh();CHECK(acquire(0,&t[0])==0);f->manager.next_sequence=UINT64_MAX;
    CHECK(bno_retire_private(&f->manager,&t[0])==BNO_RETAINED);CHECK(total_refs()==3 && !f->calls[BNO_COMPLETE]);
    fresh();CHECK(acquire(0,&t[0])==0);f->bad_view=1;
    CHECK(bno_zero_private(&f->manager,&t[0])==BNO_RETAINED);CHECK(!f->zeroed && total_refs()==3);
    CHECK(inspect(0).halted);CHECK(acquire(1,&t[1])==BNO_BUSY);
    fresh();CHECK(acquire(0,&t[0])==0);f->bad_view=13;
    CHECK(bno_zero_private(&f->manager,&t[0])==BNO_RETAINED);CHECK(inspect(0).halted);
    CHECK(acquire(1,&t[1])==BNO_BUSY && total_refs()==3);
    for(unsigned mode=18;mode<=19;mode++) {
        fresh();CHECK(acquire(0,&t[0])==0);f->bad_view=mode;
        CHECK(acquire(1,&t[1])==BNO_VALIDATION);CHECK(total_refs()==3 && !f->zeroed);
    }
    fresh();f->bad_view=20;CHECK(acquire(0,&t[0])==BNO_RETAINED);
    CHECK(total_refs()==3 && !f->calls[BNO_PREPARE] && !f->calls[BNO_RESET_REF]);
    top_policy=1;fresh();CHECK(acquire(0,&t[0])==0);CHECK(bno_zero_private(&f->manager,&t[0])==0);
    CHECK(bno_retire_private(&f->manager,&t[0])==0);top_policy=0;
}
static void *worker(void *ctx)
{struct bno_token *t=ctx;return (void*)(intptr_t)acquire(0,t);}
static void concurrency(void)
{
    fresh();f->block=1;struct bno_token t={0};pthread_t thread;CHECK(!pthread_create(&thread,NULL,worker,&t));
    CHECK(!pthread_mutex_lock(&f->wait_mutex));while(!f->waiting)CHECK(!pthread_cond_wait(&f->cond,&f->wait_mutex));
    struct bno_token second={0};CHECK(acquire(1,&second)==BNO_BUSY);CHECK(inspect(0).call_pending);
    f->unblock=1;CHECK(!pthread_cond_broadcast(&f->cond));CHECK(!pthread_mutex_unlock(&f->wait_mutex));
    void *result=NULL;CHECK(!pthread_join(thread,&result));CHECK((intptr_t)result==0);CHECK(total_refs()==3);
}
static void init_refusals(void)
{
    for(unsigned mode=0;mode<6;mode++) {
        fresh();struct bno_manager *m=calloc(1,sizeof *m);CHECK(m);
        struct bno_ops ops={f,take,drop,invoke,NULL,0};struct bpl_policy p=policy();struct bno_protection pro=protection();
        uint64_t page=BPL_PAGE;if(mode==0)page=4096;if(mode==1)pro.complete=0;if(mode==2)pro.provenance=0;
        if(mode==3)pro.pa_count=0;if(mode==4)pro.cpu_count=0;if(mode==5)pro.pa[0].base=UINT64_C(0x1000000000);
        CHECK(bno_init(m,&ops,&p,&pro,page,(struct bpl_range){(uint64_t)(uintptr_t)m,sizeof *m})!=0);
        CHECK(!f->trace_count);free(m);
    }
}
int main(void)
{
    healthy();allocation_and_prepare_faults();validation_and_retirement_faults();remaining_boundaries();concurrency();init_refusals();
    dispose();printf("PRIVATE BACKING HOST PASS checks=%u; no native runtime or DMA proof\n",checks);return 0;
}
