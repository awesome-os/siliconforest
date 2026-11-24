/* Original owned-effect tests. Reuse original private fixture, not a second owner. */
#define main private_prefix_fixture_main
#include "test_owned.c"
#undef main
static unsigned effect_calls,applied,effect_fault,mutate_request;
unsigned block_effect;
static struct bno_effect_request *caller_request;
static void effect_dispatch(void *ctx,const struct bno_effect_call *c,struct bno_effect_return *out)
{
    CHECK(ctx==f && !atomic_load(&f->held));
    struct bno_job *j=&f->manager.jobs[c->slot];struct bpl_snapshot core;
    CHECK(c->ticket.lease.registry==&f->manager.registry && c->ticket.lease.slot==j->lease.slot &&
        c->ticket.lease.generation==j->lease.generation);
    CHECK(!bpl_inspect(&f->manager.registry,&j->lease,&core));
    CHECK(core.pending==c->ticket.sequence && core.effect_count==effect_calls+1 &&
        core.ever_resources && core.state==BPL_EFFECT_RECORDED && j->phase==BNO_EFFECT_PENDING);
    static const unsigned masks[]={0,8,7,12,12,16},kinds[]={0,1,2,3,5,6};
    CHECK(c->resources==masks[c->operation] && c->kind==kinds[c->operation]);
    CHECK(j->sequence==c->sequence && j->call_pending && total_refs()==3);
    for(unsigned i=0;i<3;i++)CHECK(c->identities[i]==(uint64_t)(uintptr_t)f->refs[c->slot][i] &&
        c->views[i].cpu==(uint64_t)(uintptr_t)f->refs[c->slot][i]->page &&
        c->views[i].pa==f->refs[c->slot][i]->pa && f->refs[c->slot][i]->wire==2 &&
        j->records[i].owns_pair && !j->records[i].pair_balanced);
    struct bno_token t={&f->manager,c->generation,c->slot};struct bno_effect_result ignored={0};
    struct bno_request other={0x111,9,128};struct bno_token nt={0};
    CHECK(bno_retire_private(&f->manager,&t)==BNO_BUSY);
    CHECK(bno_zero_private(&f->manager,&t)==BNO_BUSY);
    CHECK(bno_copy_private_data(&f->manager,&t,0,&other,sizeof other)==BNO_BUSY);
    CHECK(bno_run(&f->manager,&t,&c->request,&ignored)==BNO_BUSY);
    CHECK(bno_acquire(&f->manager,&other,0x900000,BPL_READ,&nt)==BNO_BUSY);
    if(c->operation==3 || c->operation==4)CHECK(c->request.length && c->request.length<=64 &&
        c->request.offset<=BPL_PAGE && c->request.length<=BPL_PAGE-c->request.offset);
    effect_calls++;
    *out=(struct bno_effect_return){c->sequence,c->generation,c->slot,c->operation,c->ticket.sequence,
        BPL_REPORTED_SUCCESS,0,{c->identities[0],c->identities[1],c->identities[2]}};
    if(mutate_request){caller_request->operation=0;caller_request->words[0]=0xff;CHECK(c->request.operation==c->operation && c->request.words[0]==0x5a);}
    if(block_effect) {
        CHECK(!pthread_mutex_lock(&f->wait_mutex));f->waiting=1;CHECK(!pthread_cond_broadcast(&f->cond));
        while(!f->unblock)CHECK(!pthread_cond_wait(&f->cond,&f->wait_mutex));
        CHECK(!pthread_mutex_unlock(&f->wait_mutex));block_effect=0;
    }
    if(effect_fault!=1) {
        applied++;
        if(c->operation==BNO_OP_DESCRIPTOR || c->operation==BNO_OP_SUBMIT)
            memset((uint8_t*)f->refs[c->slot][2]->page+c->request.offset,(uint8_t)c->request.words[0],c->request.length);
    }
    switch(effect_fault) {
    case 1:case 2:out->outcome=BPL_REPORTED_FAILURE;out->error=-901;break;
    case 3:out->outcome=BPL_UNKNOWN;out->error=-902;break;
    case 4:out->sequence++;break;case 5:out->generation++;break;case 6:out->slot++;break;
    case 7:out->operation++;break;case 8:out->ticket_sequence++;break;case 9:out->identities[2]++;break;
    case 10:out->outcome=99;break;case 11:out->error=-999;break;
    case 12:out->outcome=BPL_REPORTED_FAILURE;break;
    case 13:memset(out,0,sizeof *out);break;default:break;
    }
}
static struct bno_token ready(uint32_t permissions)
{
    fixture_dispatch=effect_dispatch;fresh();effect_calls=applied=effect_fault=mutate_request=0;caller_request=NULL;
    struct bno_request q={0x100,0,1};struct bno_token t={0};
    memset(&caller_boot_ops,0,sizeof caller_boot_ops); /* copied once, not borrowed */
    CHECK(!bno_acquire(&f->manager,&q,0x100000,permissions,&t));CHECK(!bno_zero_private(&f->manager,&t));
    return t;
}
static struct bno_effect_request request_for(unsigned op)
{return (struct bno_effect_request){op,0,op==3 || op==4?8:0,op==3 || op==4?16:0,{0x5a,0}};}
static void budget_and_shapes(void)
{
    struct bno_token t=ready(BPL_READ|BPL_WRITE);struct bno_effect_result out={0};
    for(unsigned n=0;n<32;n++) {
        struct bno_effect_request q=request_for(n%5+1);CHECK(!bno_run(&f->manager,&t,&q,&out));
        CHECK(out.ticket_sequence==n+1 && effect_calls==n+1 && applied==n+1);
        CHECK(inspect(0).phase==BNO_EFFECT_RETAINED && inspect(0).effect_count==n+1 && !inspect(0).effect_pending && !inspect(0).call_pending);
        CHECK(bno_retire_private(&f->manager,&t)==BNO_RETAINED);
        CHECK(bno_zero_private(&f->manager,&t)==BNO_RETAINED);
        CHECK(bno_copy_private_data(&f->manager,&t,0,&n,sizeof n)==BNO_RETAINED);
    }
    struct bno_effect_request q=request_for(1);unsigned calls=f->trace_count;struct bno_effect_result saved=out;
    CHECK(bno_run(&f->manager,&t,&q,&out)==BNO_LIMIT && f->trace_count==calls && !memcmp(&out,&saved,sizeof out));
    CHECK(!f->calls[BNO_COMPLETE] && !f->calls[BNO_RESET_REF] && total_refs()==3);
    struct bno_token duplicate={0};CHECK(acquire(0,&duplicate)==BPL_CONFLICT);
    memset(&t,0,sizeof t);CHECK(total_refs()==3 && inspect(0).effect_count==32);
}
static void faults(void)
{
    for(unsigned fault=1;fault<=13;fault++) {
        struct bno_token t=ready(BPL_READ|BPL_WRITE);effect_fault=fault;
        struct bno_effect_request q=request_for(4);struct bno_effect_result out={123,456,789},saved=out;
        CHECK(bno_run(&f->manager,&t,&q,&out)==BNO_RETAINED && !memcmp(&out,&saved,sizeof out));
        struct bno_snapshot s=inspect(0);CHECK(s.halted && s.phase==BNO_QUARANTINED && s.effect_count==1);
        CHECK(total_refs()==3 && !f->calls[BNO_COMPLETE] && !f->calls[BNO_RESET_REF] && applied==(fault!=1));
        CHECK(bno_run(&f->manager,&t,&q,&out)==BNO_BUSY && effect_calls==1);
        struct bno_token n={0};CHECK(acquire(1,&n)==BNO_BUSY);
        CHECK(bno_retire_private(&f->manager,&t)==BNO_BUSY && inspect(0).first_error==s.first_error);
    }
    /* After one successful effect, changed view blocks another and all new jobs. */
    for(unsigned bad=1;bad<=19;bad++) {
        struct bno_token t=ready(BPL_READ|BPL_WRITE);struct bno_effect_request q=request_for(1);struct bno_effect_result out={0};
        CHECK(!bno_run(&f->manager,&t,&q,&out));f->bad_view=bad;
        CHECK(bno_run(&f->manager,&t,&q,&out)==BNO_RETAINED && effect_calls==1 && inspect(0).halted);
        CHECK(total_refs()==3 && !f->calls[BNO_COMPLETE] && !f->calls[BNO_RESET_REF]);
    }
    for(unsigned action=0;action<3;action++)for(unsigned mode=BEFORE;mode<=STALE;mode++) {
        struct bno_token t=ready(BPL_READ|BPL_WRITE);f->fail_action=BNO_QUERY;f->fail_role=action;f->fault=mode;
        struct bno_effect_request q=request_for(1);struct bno_effect_result out={0};
        CHECK(bno_run(&f->manager,&t,&q,&out)==BNO_RETAINED && !effect_calls && inspect(0).halted && !inspect(0).effect_count);
    }
}
static void mutate_during_query(void)
{
    CHECK(caller_request);caller_request->operation=0;caller_request->words[0]=0xff;
    fixture_query_hook=NULL;
}
static void boundary_inputs(void)
{
    struct bno_token t=ready(BPL_READ|BPL_WRITE);struct bno_effect_request q=request_for(4);struct bno_effect_result out={123,456,789};
    struct bno_token wrong=t;wrong.generation++;CHECK(bno_run(&f->manager,&wrong,&q,&out)==BNO_STALE);
    wrong=t;wrong.anchor=&wrong;CHECK(bno_run(&f->manager,&wrong,&q,&out)==BNO_STALE);
    unsigned before=f->trace_count;
    for(unsigned n=0;n<8;n++) {
        struct bno_effect_request invalid=q;
        switch(n){case 0:invalid.operation=0;break;case 1:invalid.operation=6;break;case 2:invalid.reserved=1;break;
        case 3:invalid.length=0;break;case 4:invalid.length=65;break;case 5:invalid.offset=BPL_PAGE;break;
        case 6:invalid.offset=UINT64_MAX;break;case 7:invalid.operation=1;break;}
        CHECK(bno_run(&f->manager,&t,&invalid,&out)==BNO_ARGUMENT && f->trace_count==before);
    }
    CHECK(bno_run(&f->manager,&t,&q,(struct bno_effect_result*)&q)==BNO_ARGUMENT);
    CHECK(bno_run(&f->manager,&t,&q,(struct bno_effect_result*)&t)==BNO_ARGUMENT);
    CHECK(bno_run(&f->manager,&t,&q,(struct bno_effect_result*)&f->manager)==BNO_ARGUMENT);
    CHECK(bno_run(&f->manager,&t,&q,f->refs[0][2]->page)==BNO_ARGUMENT);
    CHECK(bno_run(&f->manager,f->refs[0][0]->page,&q,&out)==BNO_ARGUMENT);
    CHECK(bno_run(&f->manager,&t,f->refs[0][0]->page,&out)==BNO_ARGUMENT);
    CHECK(bno_inspect(&f->manager,0,f->refs[0][2]->page)==BNO_ARGUMENT);
    struct bno_request extra={0x101,1,2};
    CHECK(bno_acquire(&f->manager,&extra,0x200000,BPL_READ,f->refs[0][2]->page)==BNO_ARGUMENT);
    CHECK(out.sequence==123 && out.operation==456 && out.ticket_sequence==789 && !inspect(0).halted);
    caller_request=&q;mutate_request=1;fixture_query_hook=mutate_during_query;CHECK(!bno_run(&f->manager,&t,&q,&out));CHECK(((uint8_t*)f->refs[0][2]->page)[8]==0x5a);
    t=ready(BPL_READ);q=request_for(4);CHECK(bno_run(&f->manager,&t,&q,&out)==BNO_ARGUMENT && !effect_calls);
    for(unsigned n=0;n<4;n++) {
        t=ready(BPL_READ|BPL_WRITE);f->manager.next_sequence=UINT64_MAX-n; /* test seeding only */
        q=request_for(1);before=f->trace_count;
        CHECK(bno_run(&f->manager,&t,&q,&out)==BNO_LIMIT && f->trace_count==before && !inspect(0).effect_count);
    }
    t=ready(BPL_READ|BPL_WRITE);f->manager.next_sequence=UINT64_MAX-4;
    q=request_for(1);CHECK(!bno_run(&f->manager,&t,&q,&out));
    CHECK(out.sequence==UINT64_MAX-1 && f->manager.next_sequence==UINT64_MAX);
    CHECK(bno_run(&f->manager,&t,&q,&out)==BNO_LIMIT && effect_calls==1);
}
struct pending_test {struct bno_token token;struct bno_effect_request request;struct bno_effect_result result;int status;};
static void *effect_thread(void *ctx)
{struct pending_test *p=ctx;p->status=bno_run(&f->manager,&p->token,&p->request,&p->result);return NULL;}
static void pending_lifetime(void)
{
    struct pending_test p={ready(BPL_READ|BPL_WRITE),request_for(1),{0},0};
    // Provider query must not suspend first: block only after its three rechecks.
    f->block=0;effect_fault=3;
    // Hold the effect callback by using its own flag instead of provider flag.
    f->waiting=0;f->unblock=0;
    pthread_t thread;
    block_effect=1;CHECK(!pthread_create(&thread,NULL,effect_thread,&p));
    CHECK(!pthread_mutex_lock(&f->wait_mutex));while(!f->waiting)CHECK(!pthread_cond_wait(&f->cond,&f->wait_mutex));
    CHECK(!pthread_mutex_unlock(&f->wait_mutex));
    CHECK(inspect(0).busy && inspect(0).effect_pending==1 && total_refs()==3);
    CHECK(bno_retire_private(&f->manager,&p.token)==BNO_BUSY);
    struct bno_token n={0};CHECK(acquire(1,&n)==BNO_BUSY);
    CHECK(!pthread_mutex_lock(&f->wait_mutex));f->unblock=1;CHECK(!pthread_cond_broadcast(&f->cond));CHECK(!pthread_mutex_unlock(&f->wait_mutex));
    CHECK(!pthread_join(thread,NULL));CHECK(p.status==BNO_RETAINED && total_refs()==3 && inspect(0).halted);
}
int main(void)
{
    budget_and_shapes();faults();boundary_inputs();pending_lifetime();dispose();
    printf("OWNED EFFECT HOST PASS checks=%u; no native effect backend\n",atomic_load(&checks));return 0;
}
