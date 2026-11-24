/* Original bounded native-ownership control flow; frozen lifecycle linked as C. */
#include "owned.h"
#include <string.h>
#define MAGIC UINT64_C(0x424e4f4f574e3031)
static int valid(const struct bno_manager *m) { return m && m->magic==MAGIC; }
static void lock(struct bno_manager *m) { m->ops.lock(m->ops.context); }
static void unlock(struct bno_manager *m) { m->ops.unlock(m->ops.context); }
static int extent(uint64_t p,uint64_t n) { return n && p<=UINT64_MAX-n; }
static int overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t k)
{ return !extent(a,n) || !extent(b,k) || (a<b+k && b<a+n); }
static int control_alias(const struct bno_manager *m,const void *p,size_t n)
{ return overlap((uint64_t)(uintptr_t)p,n,m->control.base,m->control.bytes); }
static int external_span(struct bno_manager *,const void *,size_t);
static int private_core(struct bno_manager *m,struct bno_job *j)
{
    struct bpl_snapshot s;
    if (bpl_inspect(&m->registry,&j->lease,&s)) return 0;
    return (s.state==BPL_CLAIMED || s.state==BPL_PRIVATE_METADATA) &&
        !s.effect_count && !s.pending && !s.ever_resources;
}
static int retained_core(struct bno_manager *m,struct bno_job *j)
{
    struct bpl_snapshot s;
    if(bpl_inspect(&m->registry,&j->lease,&s))return 0;
    if(!j->metadata_bound || j->core_retired || s.span_count!=BPL_SPANS || s.pending)return 0;
    if(j->phase==BNO_INITIALIZED)return private_core(m,j);
    return j->phase==BNO_EFFECT_RETAINED && s.state==BPL_EFFECT_RECORDED &&
        s.effect_count && s.ever_resources;
}
static int call_allowed(struct bno_manager *m,struct bno_job *j,uint32_t action)
{
    return private_core(m,j) || (action==BNO_QUERY && retained_core(m,j)) ||
        (j->core_retired && action==BNO_RESET_REF);
}
static int retain(struct bno_manager *m,struct bno_job *j,int error)
{
    if (!j->first_error) j->first_error=error?error:BNO_RETAINED;
    j->phase=BNO_QUARANTINED;
    /* Any uncertainty permanently halts the domain. Even formerly bound views
     * may have changed; stale spans cannot admit another cohort. After core
     * retirement a reset uncertainty can mean some refs already disappeared. */
    m->halted=1;
    if (!j->core_retired) (void)bpl_quarantine(&m->registry,&j->lease,j->first_error);
    return BNO_RETAINED;
}
static int token_job(struct bno_manager *m,const struct bno_token *t,struct bno_job **out)
{
    if (!t || t->anchor!=m || t->slot>=BNO_JOBS || !t->generation) return BNO_STALE;
    struct bno_job *j=&m->jobs[t->slot];
    if (j->generation!=t->generation || j->phase==BNO_EMPTY || j->phase==BNO_REFS_RELEASED) return BNO_STALE;
    *out=j; return BNO_OK;
}
/* Called with short lock held; returns with it held. The busy owner cannot be
 * replaced by any public API while the native callback is outside the lock. */
static int call(struct bno_manager *m,struct bno_job *j,uint32_t role,uint32_t action,
                 uint64_t offset,const void *source,uint64_t bytes,struct bno_return *result)
{
    if (!m->next_sequence || m->next_sequence==UINT64_MAX) return retain(m,j,BNO_LIMIT);
    if (!call_allowed(m,j,action)) return retain(m,j,BNO_STALE);
    struct bno_record *r=&j->records[role];
    struct bno_call c={m->next_sequence++,j->generation,r->identity,offset,bytes,
        j->lease.slot,role,action,source,r->view};
    j->sequence=c.sequence;j->call_pending=1;j->last_action=action;j->last_role=role;
    if(action==BNO_ALLOCATE)r->alloc_started=1;
    if(action==BNO_PREPARE)r->prepare_started=1;
    if(action==BNO_COMPLETE)r->complete_started=1;
    if(action==BNO_RESET_REF)r->reset_started=1;
    *result=(struct bno_return){0};
    unlock(m);
    m->ops.invoke(m->ops.context,&c,result);
    lock(m);
    if (!m->busy || j->generation!=c.generation || j->sequence!=c.sequence ||
        !j->call_pending || result->sequence!=c.sequence || result->generation!=c.generation ||
        result->slot!=c.slot || result->role!=role || result->action!=action ||
        result->outcome>BNO_CALL_UNKNOWN ||
        (result->outcome==BNO_CALL_OK && result->native_error)) return retain(m,j,BNO_STALE);
    j->call_pending=0;
    if(action==BNO_ALLOCATE) {
        r->alloc_returned=1;r->identity=result->identity;
    } else if(action==BNO_RESET_REF) {
        if(result->outcome==BNO_CALL_OK && !result->identity) r->reset_returned=1;
    } else if(result->identity!=r->identity || !r->identity) return retain(m,j,BNO_STALE);
    if(action==BNO_PREPARE)r->prepare_returned=1;
    if(action==BNO_COMPLETE)r->complete_returned=1;
    if(result->outcome==BNO_CALL_UNKNOWN) return retain(m,j,result->native_error);
    if (!call_allowed(m,j,action)) return retain(m,j,BNO_STALE);
    return BNO_OK;
}
static int identities_unique(struct bno_manager *m,struct bno_job *j)
{
    for(unsigned i=0;i<BNO_ROLES;i++) {
        uint64_t id=j->records[i].identity;
        if(!id)return 0;
        for(unsigned a=0;a<BNO_JOBS;a++) for(unsigned b=0;b<BNO_ROLES;b++) {
            struct bno_job *other=&m->jobs[a];
            if(other->phase==BNO_EMPTY || other->phase==BNO_REFS_RELEASED ||
                (other==j && b>=i))continue;
            if(id==other->records[b].identity)return 0;
        }
    }
    return 1;
}
static int views_valid(struct bno_manager *m,struct bno_job *j,struct bpl_span spans[3])
{
    if(!identities_unique(m,j))return BNO_VALIDATION;
    for(unsigned i=0;i<BNO_ROLES;i++) {
        struct bno_record *r=&j->records[i];struct bno_view v=r->view;
        if(v.length!=BPL_PAGE || v.capacity!=BPL_PAGE || v.span!=BPL_PAGE || !v.cpu ||
            !extent(v.cpu,BPL_PAGE) || !extent(v.pa,BPL_PAGE) ||
            v.pa+BPL_PAGE>UINT64_C(0x1000000000) || ((v.cpu|v.pa)&(BPL_PAGE-1)) ||
            overlap(v.cpu,BPL_PAGE,m->control.base,m->control.bytes))return BNO_VALIDATION;
        for(unsigned k=0;k<m->protection.pa_count;k++) {
            struct bpl_range p=m->protection.pa[k];
            if(overlap(v.pa,BPL_PAGE,p.base,p.bytes))return BNO_VALIDATION;
        }
        for(unsigned k=0;k<m->protection.cpu_count;k++) {
            struct bpl_range p=m->protection.cpu[k];
            if(overlap(v.cpu,BPL_PAGE,p.base,p.bytes))return BNO_VALIDATION;
        }
        for(unsigned a=0;a<BNO_JOBS;a++)for(unsigned b=0;b<BNO_ROLES;b++) {
            struct bno_job *o=&m->jobs[a];struct bno_view w=o->records[b].view;
            if(o->phase==BNO_EMPTY || o->phase==BNO_REFS_RELEASED || (o==j && b>=i) || !w.cpu)continue;
            if(overlap(v.cpu,BPL_PAGE,w.cpu,BPL_PAGE) || overlap(v.pa,BPL_PAGE,w.pa,BPL_PAGE))return BNO_VALIDATION;
        }
        spans[i]=(struct bpl_span){i+1,i==2?j->permissions:0,r->handle,j->generation,
            v.cpu,v.pa,BPL_PAGE,i==2?j->iova:0};
    }
    return BNO_OK;
}
/* Cleanup is invoked only when all owned pair outcomes are known. */
static int retire(struct bno_manager *m,struct bno_job *j)
{
    if(!private_core(m,j))return retain(m,j,BNO_STALE);
    j->phase=BNO_RETIRING;
    for(unsigned n=BNO_ROLES;n>0;n--) {
        unsigned i=n-1;struct bno_record *r=&j->records[i];
        if(r->prepare_started && (!r->prepare_returned || !r->owns_pair))return retain(m,j,BNO_RETAINED);
        if(r->owns_pair && !r->pair_balanced) {
            if(r->complete_started)return retain(m,j,BNO_RETAINED);
            struct bno_return result;
            int rc=call(m,j,i,BNO_COMPLETE,0,NULL,0,&result);if(rc)return rc;
            if(result.outcome!=BNO_CALL_OK)return retain(m,j,result.native_error);
            r->pair_balanced=1;
        }
    }
    if(!private_core(m,j) || bpl_retire_private(&m->registry,&j->lease))return retain(m,j,BNO_STALE);
    j->core_retired=1;j->phase=BNO_CORE_RETIRED;
    for(unsigned i=0;i<BNO_ROLES;i++)j->records[i].view=(struct bno_view){0};
    for(unsigned n=BNO_ROLES;n>0;n--) {
        unsigned i=n-1;struct bno_record *r=&j->records[i];
        if(!r->identity)continue;
        struct bno_return result;int rc=call(m,j,i,BNO_RESET_REF,0,NULL,0,&result);if(rc)return rc;
        if(result.outcome!=BNO_CALL_OK || result.identity || !r->reset_returned)return retain(m,j,BNO_RETAINED);
        r->identity=0;
    }
    j->phase=BNO_REFS_RELEASED;return BNO_OK;
}
int bno_init(struct bno_manager *m,const struct bno_ops *ops,const struct bpl_policy *policy,
             const struct bno_protection *protection,uint64_t pagesize,struct bpl_range control)
{
    if(!m || !ops || !policy || !ops->lock || !ops->unlock || !ops->invoke ||
        ops->effect_backend_available>1 || (!!ops->dispatch!=!!ops->effect_backend_available) ||
        !extent(control.base,control.bytes) || (uint64_t)(uintptr_t)m<control.base ||
        sizeof *m>control.bytes || (uint64_t)(uintptr_t)m-control.base>control.bytes-sizeof *m)
        return BNO_ARGUMENT;
    if(pagesize!=BPL_PAGE)return BNO_PAGE;
    if(!protection || protection->complete!=1 || !protection->provenance ||
        !protection->pa_count || protection->pa_count>BPL_RANGES ||
        !protection->cpu_count || protection->cpu_count>BPL_RANGES)return BNO_VALIDATION;
    for(unsigned i=0;i<protection->pa_count;i++) {
        struct bpl_range p=protection->pa[i];
        if(!extent(p.base,p.bytes) || p.base+p.bytes>UINT64_C(0x1000000000))return BNO_VALIDATION;
    }
    for(unsigned i=0;i<protection->cpu_count;i++)
        if(!extent(protection->cpu[i].base,protection->cpu[i].bytes))return BNO_VALIDATION;
    const unsigned char *p=(const unsigned char *)m;
    for(size_t i=0;i<sizeof *m;i++)if(p[i])return BNO_ARGUMENT;
    if(overlap((uint64_t)(uintptr_t)ops,sizeof *ops,control.base,control.bytes) ||
       overlap((uint64_t)(uintptr_t)policy,sizeof *policy,control.base,control.bytes) ||
       overlap((uint64_t)(uintptr_t)protection,sizeof *protection,control.base,control.bytes))return BNO_ARGUMENT;
    int rc=bpl_init(&m->registry,policy);if(rc)return rc;
    m->ops=*ops;m->control=control;m->protection=*protection;m->next_handle=1;m->next_sequence=1;m->magic=MAGIC;
    return BNO_OK;
}
int bno_acquire(struct bno_manager *m,const struct bno_request *request,uint64_t iova,
                uint32_t permissions,struct bno_token *out)
{
    if(!valid(m) || !request || !out || control_alias(m,request,sizeof *request) ||
        control_alias(m,out,sizeof *out) || overlap((uint64_t)(uintptr_t)out,sizeof *out,(uint64_t)(uintptr_t)request,sizeof *request))return BNO_ARGUMENT;
    if(iova<0x100000 || iova>UINT64_C(0xbff00000)-BPL_PAGE || (iova&(BPL_PAGE-1)) ||
        !permissions || (permissions&~(BPL_READ|BPL_WRITE)))return BNO_VALIDATION;
    lock(m);
    if(m->busy || m->halted){unlock(m);return BNO_BUSY;}
    if(!external_span(m,request,sizeof *request) || !external_span(m,out,sizeof *out)){unlock(m);return BNO_ARGUMENT;}
    if(!m->next_handle || m->next_handle>UINT64_MAX-3 || !m->next_sequence || m->next_sequence==UINT64_MAX){unlock(m);return BNO_LIMIT;}
    struct bpl_request claimed={request->rid,request->sid,request->vectors};
    struct bpl_lease lease;int rc=bpl_claim(&m->registry,&claimed,&lease);
    if(rc){unlock(m);return rc;}
    struct bno_job *j=&m->jobs[lease.slot];
    if(j->phase!=BNO_EMPTY && j->phase!=BNO_REFS_RELEASED){m->halted=1;unlock(m);return BNO_RETAINED;}
    memset(j,0,sizeof *j);j->lease=lease;j->generation=lease.generation;
    j->iova=iova;j->permissions=permissions;j->phase=BNO_ACQUIRING;m->busy=1;
    for(unsigned i=0;i<3;i++)j->records[i].handle=m->next_handle++;
    for(unsigned i=0;i<3;i++) {
        struct bno_return result;rc=call(m,j,i,BNO_ALLOCATE,0,NULL,BPL_PAGE,&result);if(rc)goto done;
        if(result.outcome==BNO_CALL_REFUSED && !result.identity){rc=retire(m,j);if(!rc)rc=BNO_REFUSED;goto done;}
        if(result.outcome!=BNO_CALL_OK || !result.identity){rc=retain(m,j,BNO_RETAINED);goto done;}
    }
    if(!identities_unique(m,j)){rc=retain(m,j,BNO_VALIDATION);goto done;}
    for(unsigned i=0;i<3;i++) {
        struct bno_return result;rc=call(m,j,i,BNO_PREPARE,0,NULL,0,&result);if(rc)goto done;
        if(result.outcome!=BNO_CALL_OK){rc=retain(m,j,result.native_error);goto done;}
        j->records[i].owns_pair=1;
    }
    for(unsigned i=0;i<3;i++) {
        struct bno_return result;rc=call(m,j,i,BNO_QUERY,0,NULL,0,&result);if(rc)goto done;
        if(result.outcome!=BNO_CALL_OK){rc=retire(m,j);if(!rc)rc=BNO_VALIDATION;goto done;}
        j->records[i].view=result.view;
    }
    {
        struct bpl_span spans[3];rc=views_valid(m,j,spans);
        if(!rc)rc=bpl_bind_metadata(&m->registry,&j->lease,spans);
        if(rc){rc=retire(m,j);if(!rc)rc=BNO_VALIDATION;goto done;}
    }
    j->metadata_bound=1;j->phase=BNO_VALIDATED;
    if(!external_span(m,out,sizeof *out)){rc=retain(m,j,BNO_VALIDATION);goto done;}
    *out=(struct bno_token){m,j->generation,lease.slot};rc=BNO_OK;
done:
    m->busy=0;unlock(m);return rc;
}
static int start(struct bno_manager *m,const struct bno_token *t,struct bno_job **j)
{
    if(m->busy || m->halted)return BNO_BUSY;
    int rc=token_job(m,t,j);if(rc)return rc;
    if((*j)->phase!=BNO_VALIDATED && (*j)->phase!=BNO_INITIALIZED)return BNO_RETAINED;
    if(!private_core(m,*j))return retain(m,*j,BNO_STALE);
    m->busy=1;return BNO_OK;
}
static int recheck(struct bno_manager *m,struct bno_job *j)
{
    for(unsigned i=0;i<3;i++) {
        struct bno_return result;int rc=call(m,j,i,BNO_QUERY,0,NULL,0,&result);if(rc)return rc;
        if(result.outcome!=BNO_CALL_OK || memcmp(&result.view,&j->records[i].view,sizeof result.view))return retain(m,j,BNO_VALIDATION);
    }
    struct bpl_span spans[3];int rc=views_valid(m,j,spans);
    return rc?retain(m,j,rc):BNO_OK;
}
int bno_zero_private(struct bno_manager *m,const struct bno_token *t)
{
    if(!valid(m) || !t || control_alias(m,t,sizeof *t))return BNO_ARGUMENT;
    lock(m);
    if(!external_span(m,t,sizeof *t)){unlock(m);return BNO_ARGUMENT;}
    struct bno_job *j;int rc=start(m,t,&j);if(rc){unlock(m);return rc;}
    if(j->phase!=BNO_VALIDATED){m->busy=0;unlock(m);return BNO_ARGUMENT;}
    rc=recheck(m,j);
    if(!rc)for(unsigned i=0;i<3;i++) {
        struct bno_return result;rc=call(m,j,i,BNO_ZERO,0,NULL,BPL_PAGE,&result);if(rc)break;
        if(result.outcome!=BNO_CALL_OK){rc=retain(m,j,result.native_error);break;}
    }
    if(!rc)j->phase=BNO_INITIALIZED;
    m->busy=0;unlock(m);return rc;
}
int bno_copy_private_data(struct bno_manager *m,const struct bno_token *t,uint64_t offset,const void *src,size_t n)
{
    if(!valid(m) || !t || !src || !n || offset>BPL_PAGE || n>BPL_PAGE-offset ||
        control_alias(m,t,sizeof *t) || control_alias(m,src,n))return BNO_ARGUMENT;
    lock(m);
    if(!external_span(m,t,sizeof *t)){unlock(m);return BNO_ARGUMENT;}
    struct bno_job *j;int rc=start(m,t,&j);if(rc){unlock(m);return rc;}
    if(j->phase!=BNO_INITIALIZED){rc=BNO_ARGUMENT;goto done;}
    for(unsigned i=0;i<m->protection.cpu_count;i++) {
        struct bpl_range p=m->protection.cpu[i];
        if(overlap((uint64_t)(uintptr_t)src,n,p.base,p.bytes)){rc=BNO_ARGUMENT;goto done;}
    }
    for(unsigned a=0;a<4;a++)for(unsigned b=0;b<3;b++) {
        struct bno_job *o=&m->jobs[a];uint64_t cpu=o->records[b].view.cpu;
        if(o->phase==BNO_EMPTY || o->phase==BNO_REFS_RELEASED || !cpu)continue;
        if(overlap((uint64_t)(uintptr_t)src,n,cpu,BPL_PAGE)){rc=BNO_ARGUMENT;goto done;}
    }
    rc=recheck(m,j);
    if(!rc){struct bno_return result;rc=call(m,j,2,BNO_COPY_DATA,offset,src,n,&result);
        if(!rc && result.outcome!=BNO_CALL_OK)rc=retain(m,j,result.native_error);}
done:
    m->busy=0;unlock(m);return rc;
}
int bno_retire_private(struct bno_manager *m,const struct bno_token *t)
{
    if(!valid(m) || !t || control_alias(m,t,sizeof *t))return BNO_ARGUMENT;
    lock(m);
    if(!external_span(m,t,sizeof *t)){unlock(m);return BNO_ARGUMENT;}
    struct bno_job *j;int rc=start(m,t,&j);if(rc){unlock(m);return rc;}
    rc=retire(m,j);m->busy=0;unlock(m);return rc;
}
int bno_inspect(struct bno_manager *m,uint32_t slot,struct bno_snapshot *out)
{
    if(!valid(m) || !out || slot>=4 || control_alias(m,out,sizeof *out))return BNO_ARGUMENT;
    lock(m);
    if(!external_span(m,out,sizeof *out)){unlock(m);return BNO_ARGUMENT;}
    const struct bno_job *j=&m->jobs[slot];
    struct bno_snapshot s={j->generation,j->sequence,j->phase,m->busy,m->halted,
        j->metadata_bound,j->core_retired,j->call_pending,0,0,0,0,j->first_error,0,0,0};
    for(unsigned i=0;i<3;i++) {const struct bno_record *r=&j->records[i];
        s.identity_records+=r->identity!=0;s.owned_pairs+=r->owns_pair;
        s.balanced_pairs+=r->pair_balanced;s.resets_returned+=r->reset_returned;}
    struct bpl_snapshot core;
    if(j->generation && !bpl_inspect(&m->registry,&j->lease,&core)) {
        s.effect_count=core.effect_count;s.effect_pending=core.pending;s.ever_resources=core.ever_resources;
    }
    *out=s;unlock(m);return BNO_OK;
}
/* All addresses remain internal. External inputs/outputs cannot overlap any
 * known owned/control span. Caller supplies stable ordinary request storage. */
static int external_span(struct bno_manager *m,const void *p,size_t n)
{
    uint64_t a=(uint64_t)(uintptr_t)p;
    if(!p || !extent(a,n) || control_alias(m,p,n))return 0;
    for(unsigned i=0;i<m->protection.cpu_count;i++)
        if(overlap(a,n,m->protection.cpu[i].base,m->protection.cpu[i].bytes))return 0;
    for(unsigned i=0;i<BNO_JOBS;i++)for(unsigned k=0;k<BNO_ROLES;k++) {
        struct bno_job *j=&m->jobs[i];
        if(!j->metadata_bound || j->phase==BNO_EMPTY || j->phase==BNO_REFS_RELEASED || !j->records[k].view.cpu)continue;
        if(overlap(a,n,j->records[k].view.cpu,BPL_PAGE))return 0;
    }
    return 1;
}
/* Fixed operation -> conservative possible-effect union; no hardware schema. */
static int effect_shape(const struct bno_effect_request *q,uint32_t permissions,
                        uint32_t *kind,uint32_t *resources)
{
    if(q->reserved || q->operation<BNO_OP_CONFIG || q->operation>BNO_OP_INTERRUPT)return BNO_ARGUMENT;
    static const uint32_t kinds[]={0,BPL_CONFIG,BPL_MAPPING,BPL_DESCRIPTOR,BPL_DOORBELL,BPL_INTERRUPT};
    static const uint32_t masks[]={0,8,7,12,12,16};
    if(q->operation==BNO_OP_DESCRIPTOR || q->operation==BNO_OP_SUBMIT) {
        if(!q->length || q->length>64 || q->offset>BPL_PAGE || q->length>BPL_PAGE-q->offset ||
            !(permissions&BPL_WRITE))return BNO_ARGUMENT;
    } else if(q->offset || q->length)return BNO_ARGUMENT;
    if(q->operation==BNO_OP_MAPPING && !(permissions&BPL_READ))return BNO_ARGUMENT;
    *kind=kinds[q->operation];*resources=masks[q->operation];return BNO_OK;
}
int bno_run(struct bno_manager *m,const struct bno_token *token,
            const struct bno_effect_request *request,struct bno_effect_result *out)
{
    if(!valid(m) || !token || !request || !out)return BNO_ARGUMENT;
    lock(m);
    if(m->busy || m->halted){unlock(m);return BNO_BUSY;}
    if(!external_span(m,token,sizeof *token) || !external_span(m,request,sizeof *request) ||
        !external_span(m,out,sizeof *out) ||
        overlap((uint64_t)(uintptr_t)out,sizeof *out,(uint64_t)(uintptr_t)token,sizeof *token) ||
        overlap((uint64_t)(uintptr_t)out,sizeof *out,(uint64_t)(uintptr_t)request,sizeof *request)) {
        unlock(m);return BNO_ARGUMENT;
    }
    /* Capture caller-controlled values before any callback. */
    const struct bno_token t=*token;
    const struct bno_effect_request q=*request;
    struct bno_job *j;int rc=token_job(m,&t,&j);
    if(rc){unlock(m);return rc;}
    if(j->phase!=BNO_INITIALIZED && j->phase!=BNO_EFFECT_RETAINED){unlock(m);return BNO_RETAINED;}
    uint32_t kind,resources;rc=effect_shape(&q,j->permissions,&kind,&resources);
    if(rc){unlock(m);return rc;}
    if(!retained_core(m,j)){rc=retain(m,j,BNO_STALE);unlock(m);return rc;}
    /* Production adapter has no effect backend: no native query or begin. */
    if(!m->ops.effect_backend_available){unlock(m);return BNO_UNAVAILABLE;}
    struct bpl_snapshot snapshot;
    if(bpl_inspect(&m->registry,&j->lease,&snapshot)){rc=retain(m,j,BNO_STALE);unlock(m);return rc;}
    if(snapshot.effect_count>=BPL_EFFECTS || !m->next_sequence || m->next_sequence>UINT64_MAX-4) {
        unlock(m);return BNO_LIMIT;
    }
    m->busy=1;
    rc=recheck(m,j);if(rc)goto done;
    /* One bounded synchronous operation, no asynchronous borrow/ticket API. */
    struct bno_effect_call call_scope={0};
    call_scope.sequence=m->next_sequence++;call_scope.generation=j->generation;
    call_scope.slot=j->lease.slot;call_scope.operation=q.operation;
    call_scope.kind=kind;call_scope.resources=resources;call_scope.request=q;
    for(unsigned i=0;i<BNO_ROLES;i++) {
        if(!j->records[i].owns_pair || j->records[i].pair_balanced || !j->records[i].identity) {
            rc=retain(m,j,BNO_RETAINED);goto done;
        }
        call_scope.identities[i]=j->records[i].identity;call_scope.views[i]=j->records[i].view;
    }
    rc=bpl_effect_begin(&m->registry,&j->lease,kind,resources,&call_scope.ticket);
    if(rc){rc=retain(m,j,rc);goto done;}
    j->phase=BNO_EFFECT_PENDING;j->call_pending=1;j->sequence=call_scope.sequence;
    struct bno_effect_return result={0};
    unlock(m);
    m->ops.dispatch(m->ops.context,&call_scope,&result);
    lock(m);
    /* Scope expires here; no publicly usable value contains it. */
    if(!m->busy || j->phase!=BNO_EFFECT_PENDING || !j->call_pending ||
        j->sequence!=call_scope.sequence || j->generation!=call_scope.generation ||
        result.sequence!=call_scope.sequence || result.generation!=call_scope.generation ||
        result.slot!=call_scope.slot || result.operation!=call_scope.operation ||
        result.ticket_sequence!=call_scope.ticket.sequence ||
        result.outcome<BPL_REPORTED_SUCCESS || result.outcome>BPL_UNKNOWN ||
        (result.outcome==BPL_REPORTED_SUCCESS?result.error!=0:result.error==0)) {
        rc=retain(m,j,BNO_STALE);goto done;
    }
    for(unsigned i=0;i<BNO_ROLES;i++)
        if(result.identities[i]!=call_scope.identities[i] || j->records[i].identity!=call_scope.identities[i]) {
            rc=retain(m,j,BNO_STALE);goto done;
        }
    rc=bpl_effect_result(&m->registry,&call_scope.ticket,result.outcome,result.error);
    if(rc || result.outcome!=BPL_REPORTED_SUCCESS){rc=retain(m,j,result.error?result.error:rc);goto done;}
    j->call_pending = 0;j->phase=BNO_EFFECT_RETAINED;
    *out=(struct bno_effect_result){call_scope.sequence,q.operation,call_scope.ticket.sequence};
    rc=BNO_OK;
done:
    m->busy=0;unlock(m);return rc;
}
