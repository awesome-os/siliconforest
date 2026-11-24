/* Original bounded native-ownership control flow; frozen lifecycle linked as C. */
#include "backing.h"
#include <string.h>
#define MAGIC UINT64_C(0x42504e414e434831)
static int valid(const struct bpn_manager *m) { return m && m->magic==MAGIC; }
static void lock(struct bpn_manager *m) { m->ops.lock(m->ops.context); }
static void unlock(struct bpn_manager *m) { m->ops.unlock(m->ops.context); }
static int extent(uint64_t p,uint64_t n) { return n && p<=UINT64_MAX-n; }
static int overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t k)
{ return !extent(a,n) || !extent(b,k) || (a<b+k && b<a+n); }
static int control_alias(const struct bpn_manager *m,const void *p,size_t n)
{ return overlap((uint64_t)(uintptr_t)p,n,m->control.base,m->control.bytes); }
static int private_core(struct bpn_manager *m,struct bpn_job *j)
{
    struct bpl_snapshot s;
    if (bpl_inspect(&m->registry,&j->lease,&s)) return 0;
    return (s.state==BPL_CLAIMED || s.state==BPL_PRIVATE_METADATA) &&
        !s.effect_count && !s.pending && !s.ever_resources;
}
static int retain(struct bpn_manager *m,struct bpn_job *j,int error)
{
    if (!j->first_error) j->first_error=error?error:BPN_RETAINED;
    j->phase=BPN_QUARANTINED;
    /* Any uncertainty permanently halts the domain. Even formerly bound views
     * may have changed; stale spans cannot admit another cohort. After core
     * retirement a reset uncertainty can mean some refs already disappeared. */
    m->halted=1;
    if (!j->core_retired) (void)bpl_quarantine(&m->registry,&j->lease,j->first_error);
    return BPN_RETAINED;
}
static int token_job(struct bpn_manager *m,const struct bpn_token *t,struct bpn_job **out)
{
    if (!t || t->anchor!=m || t->slot>=BPN_JOBS || !t->generation) return BPN_STALE;
    struct bpn_job *j=&m->jobs[t->slot];
    if (j->generation!=t->generation || j->phase==BPN_EMPTY || j->phase==BPN_REFS_RELEASED) return BPN_STALE;
    *out=j; return BPN_OK;
}
/* Called with short lock held; returns with it held. The busy owner cannot be
 * replaced by any public API while the native callback is outside the lock. */
static int call(struct bpn_manager *m,struct bpn_job *j,uint32_t role,uint32_t action,
                 uint64_t offset,const void *source,uint64_t bytes,struct bpn_return *result)
{
    if (!m->next_sequence || m->next_sequence==UINT64_MAX) return retain(m,j,BPN_LIMIT);
    if (!private_core(m,j) && !(j->core_retired && action==BPN_RESET_REF)) return retain(m,j,BPN_STALE);
    struct bpn_record *r=&j->records[role];
    struct bpn_call c={m->next_sequence++,j->generation,r->identity,offset,bytes,
        j->lease.slot,role,action,source,r->view};
    j->sequence=c.sequence;j->call_pending=1;j->last_action=action;j->last_role=role;
    if(action==BPN_ALLOCATE)r->alloc_started=1;
    if(action==BPN_PREPARE)r->prepare_started=1;
    if(action==BPN_COMPLETE)r->complete_started=1;
    if(action==BPN_RESET_REF)r->reset_started=1;
    *result=(struct bpn_return){0};
    unlock(m);
    m->ops.invoke(m->ops.context,&c,result);
    lock(m);
    if (!m->busy || j->generation!=c.generation || j->sequence!=c.sequence ||
        !j->call_pending || result->sequence!=c.sequence || result->generation!=c.generation ||
        result->slot!=c.slot || result->role!=role || result->action!=action ||
        result->outcome>BPN_CALL_UNKNOWN ||
        (result->outcome==BPN_CALL_OK && result->native_error)) return retain(m,j,BPN_STALE);
    j->call_pending=0;
    if(action==BPN_ALLOCATE) {
        r->alloc_returned=1;r->identity=result->identity;
    } else if(action==BPN_RESET_REF) {
        if(result->outcome==BPN_CALL_OK && !result->identity) r->reset_returned=1;
    } else if(result->identity!=r->identity || !r->identity) return retain(m,j,BPN_STALE);
    if(action==BPN_PREPARE)r->prepare_returned=1;
    if(action==BPN_COMPLETE)r->complete_returned=1;
    if(result->outcome==BPN_CALL_UNKNOWN) return retain(m,j,result->native_error);
    if (!private_core(m,j) && !(j->core_retired && action==BPN_RESET_REF)) return retain(m,j,BPN_STALE);
    return BPN_OK;
}
static int identities_unique(struct bpn_manager *m,struct bpn_job *j)
{
    for(unsigned i=0;i<BPN_ROLES;i++) {
        uint64_t id=j->records[i].identity;
        if(!id)return 0;
        for(unsigned a=0;a<BPN_JOBS;a++) for(unsigned b=0;b<BPN_ROLES;b++) {
            struct bpn_job *other=&m->jobs[a];
            if(other->phase==BPN_EMPTY || other->phase==BPN_REFS_RELEASED ||
                (other==j && b>=i))continue;
            if(id==other->records[b].identity)return 0;
        }
    }
    return 1;
}
static int views_valid(struct bpn_manager *m,struct bpn_job *j,struct bpl_span spans[3])
{
    if(!identities_unique(m,j))return BPN_VALIDATION;
    for(unsigned i=0;i<BPN_ROLES;i++) {
        struct bpn_record *r=&j->records[i];struct bpn_view v=r->view;
        if(v.length!=BPL_PAGE || v.capacity!=BPL_PAGE || v.span!=BPL_PAGE || !v.cpu ||
            !extent(v.cpu,BPL_PAGE) || !extent(v.pa,BPL_PAGE) ||
            v.pa+BPL_PAGE>UINT64_C(0x1000000000) || ((v.cpu|v.pa)&(BPL_PAGE-1)) ||
            overlap(v.cpu,BPL_PAGE,m->control.base,m->control.bytes))return BPN_VALIDATION;
        for(unsigned k=0;k<m->protection.pa_count;k++) {
            struct bpl_range p=m->protection.pa[k];
            if(overlap(v.pa,BPL_PAGE,p.base,p.bytes))return BPN_VALIDATION;
        }
        for(unsigned k=0;k<m->protection.cpu_count;k++) {
            struct bpl_range p=m->protection.cpu[k];
            if(overlap(v.cpu,BPL_PAGE,p.base,p.bytes))return BPN_VALIDATION;
        }
        for(unsigned a=0;a<BPN_JOBS;a++)for(unsigned b=0;b<BPN_ROLES;b++) {
            struct bpn_job *o=&m->jobs[a];struct bpn_view w=o->records[b].view;
            if(o->phase==BPN_EMPTY || o->phase==BPN_REFS_RELEASED || (o==j && b>=i) || !w.cpu)continue;
            if(overlap(v.cpu,BPL_PAGE,w.cpu,BPL_PAGE) || overlap(v.pa,BPL_PAGE,w.pa,BPL_PAGE))return BPN_VALIDATION;
        }
        spans[i]=(struct bpl_span){i+1,i==2?j->permissions:0,r->handle,j->generation,
            v.cpu,v.pa,BPL_PAGE,i==2?j->iova:0};
    }
    return BPN_OK;
}
/* Cleanup is invoked only when all owned pair outcomes are known. */
static int retire(struct bpn_manager *m,struct bpn_job *j)
{
    if(!private_core(m,j))return retain(m,j,BPN_STALE);
    j->phase=BPN_RETIRING;
    for(unsigned n=BPN_ROLES;n>0;n--) {
        unsigned i=n-1;struct bpn_record *r=&j->records[i];
        if(r->prepare_started && (!r->prepare_returned || !r->owns_pair))return retain(m,j,BPN_RETAINED);
        if(r->owns_pair && !r->pair_balanced) {
            if(r->complete_started)return retain(m,j,BPN_RETAINED);
            struct bpn_return result;
            int rc=call(m,j,i,BPN_COMPLETE,0,NULL,0,&result);if(rc)return rc;
            if(result.outcome!=BPN_CALL_OK)return retain(m,j,result.native_error);
            r->pair_balanced=1;
        }
    }
    if(!private_core(m,j) || bpl_retire_private(&m->registry,&j->lease))return retain(m,j,BPN_STALE);
    j->core_retired=1;j->phase=BPN_CORE_RETIRED;
    for(unsigned i=0;i<BPN_ROLES;i++)j->records[i].view=(struct bpn_view){0};
    for(unsigned n=BPN_ROLES;n>0;n--) {
        unsigned i=n-1;struct bpn_record *r=&j->records[i];
        if(!r->identity)continue;
        struct bpn_return result;int rc=call(m,j,i,BPN_RESET_REF,0,NULL,0,&result);if(rc)return rc;
        if(result.outcome!=BPN_CALL_OK || result.identity || !r->reset_returned)return retain(m,j,BPN_RETAINED);
        r->identity=0;
    }
    j->phase=BPN_REFS_RELEASED;return BPN_OK;
}
int bpn_init(struct bpn_manager *m,const struct bpn_ops *ops,const struct bpl_policy *policy,
             const struct bpn_protection *protection,uint64_t pagesize,struct bpl_range control)
{
    if(!m || !ops || !policy || !ops->lock || !ops->unlock || !ops->invoke ||
        !extent(control.base,control.bytes) || (uint64_t)(uintptr_t)m<control.base ||
        sizeof *m>control.bytes || (uint64_t)(uintptr_t)m-control.base>control.bytes-sizeof *m)
        return BPN_ARGUMENT;
    if(pagesize!=BPL_PAGE)return BPN_PAGE;
    if(!protection || protection->complete!=1 || !protection->provenance ||
        !protection->pa_count || protection->pa_count>BPL_RANGES ||
        !protection->cpu_count || protection->cpu_count>BPL_RANGES)return BPN_VALIDATION;
    for(unsigned i=0;i<protection->pa_count;i++) {
        struct bpl_range p=protection->pa[i];
        if(!extent(p.base,p.bytes) || p.base+p.bytes>UINT64_C(0x1000000000))return BPN_VALIDATION;
    }
    for(unsigned i=0;i<protection->cpu_count;i++)
        if(!extent(protection->cpu[i].base,protection->cpu[i].bytes))return BPN_VALIDATION;
    const unsigned char *p=(const unsigned char *)m;
    for(size_t i=0;i<sizeof *m;i++)if(p[i])return BPN_ARGUMENT;
    if(overlap((uint64_t)(uintptr_t)ops,sizeof *ops,control.base,control.bytes) ||
       overlap((uint64_t)(uintptr_t)policy,sizeof *policy,control.base,control.bytes) ||
       overlap((uint64_t)(uintptr_t)protection,sizeof *protection,control.base,control.bytes))return BPN_ARGUMENT;
    int rc=bpl_init(&m->registry,policy);if(rc)return rc;
    m->ops=*ops;m->control=control;m->protection=*protection;m->next_handle=1;m->next_sequence=1;m->magic=MAGIC;
    return BPN_OK;
}
int bpn_acquire(struct bpn_manager *m,const struct bpl_request *request,uint64_t iova,
                uint32_t permissions,struct bpn_token *out)
{
    if(!valid(m) || !request || !out || control_alias(m,request,sizeof *request) ||
        control_alias(m,out,sizeof *out) || overlap((uint64_t)(uintptr_t)out,sizeof *out,(uint64_t)(uintptr_t)request,sizeof *request))return BPN_ARGUMENT;
    if(iova<0x100000 || iova>UINT64_C(0xbff00000)-BPL_PAGE || (iova&(BPL_PAGE-1)) ||
        !permissions || (permissions&~(BPL_READ|BPL_WRITE)))return BPN_VALIDATION;
    lock(m);
    if(m->busy || m->halted){unlock(m);return BPN_BUSY;}
    if(!m->next_handle || m->next_handle>UINT64_MAX-3 || !m->next_sequence || m->next_sequence==UINT64_MAX){unlock(m);return BPN_LIMIT;}
    struct bpl_lease lease;int rc=bpl_claim(&m->registry,request,&lease);
    if(rc){unlock(m);return rc;}
    struct bpn_job *j=&m->jobs[lease.slot];
    if(j->phase!=BPN_EMPTY && j->phase!=BPN_REFS_RELEASED){m->halted=1;unlock(m);return BPN_RETAINED;}
    memset(j,0,sizeof *j);j->lease=lease;j->generation=lease.generation;
    j->iova=iova;j->permissions=permissions;j->phase=BPN_ACQUIRING;m->busy=1;
    for(unsigned i=0;i<3;i++)j->records[i].handle=m->next_handle++;
    for(unsigned i=0;i<3;i++) {
        struct bpn_return result;rc=call(m,j,i,BPN_ALLOCATE,0,NULL,BPL_PAGE,&result);if(rc)goto done;
        if(result.outcome==BPN_CALL_REFUSED && !result.identity){rc=retire(m,j);if(!rc)rc=BPN_REFUSED;goto done;}
        if(result.outcome!=BPN_CALL_OK || !result.identity){rc=retain(m,j,BPN_RETAINED);goto done;}
    }
    if(!identities_unique(m,j)){rc=retain(m,j,BPN_VALIDATION);goto done;}
    for(unsigned i=0;i<3;i++) {
        struct bpn_return result;rc=call(m,j,i,BPN_PREPARE,0,NULL,0,&result);if(rc)goto done;
        if(result.outcome!=BPN_CALL_OK){rc=retain(m,j,result.native_error);goto done;}
        j->records[i].owns_pair=1;
    }
    for(unsigned i=0;i<3;i++) {
        struct bpn_return result;rc=call(m,j,i,BPN_QUERY,0,NULL,0,&result);if(rc)goto done;
        if(result.outcome!=BPN_CALL_OK){rc=retire(m,j);if(!rc)rc=BPN_VALIDATION;goto done;}
        j->records[i].view=result.view;
    }
    {
        struct bpl_span spans[3];rc=views_valid(m,j,spans);
        if(!rc)rc=bpl_bind_metadata(&m->registry,&j->lease,spans);
        if(rc){rc=retire(m,j);if(!rc)rc=BPN_VALIDATION;goto done;}
    }
    j->metadata_bound=1;j->phase=BPN_VALIDATED;
    *out=(struct bpn_token){m,j->generation,lease.slot};rc=BPN_OK;
done:
    m->busy=0;unlock(m);return rc;
}
static int start(struct bpn_manager *m,const struct bpn_token *t,struct bpn_job **j)
{
    if(m->busy || m->halted)return BPN_BUSY;
    int rc=token_job(m,t,j);if(rc)return rc;
    if((*j)->phase!=BPN_VALIDATED && (*j)->phase!=BPN_INITIALIZED)return BPN_RETAINED;
    if(!private_core(m,*j))return retain(m,*j,BPN_STALE);
    m->busy=1;return BPN_OK;
}
static int recheck(struct bpn_manager *m,struct bpn_job *j)
{
    for(unsigned i=0;i<3;i++) {
        struct bpn_return result;int rc=call(m,j,i,BPN_QUERY,0,NULL,0,&result);if(rc)return rc;
        if(result.outcome!=BPN_CALL_OK || memcmp(&result.view,&j->records[i].view,sizeof result.view))return retain(m,j,BPN_VALIDATION);
    }
    struct bpl_span spans[3];int rc=views_valid(m,j,spans);
    return rc?retain(m,j,rc):BPN_OK;
}
int bpn_zero_private(struct bpn_manager *m,const struct bpn_token *t)
{
    if(!valid(m) || !t || control_alias(m,t,sizeof *t))return BPN_ARGUMENT;
    lock(m);struct bpn_job *j;int rc=start(m,t,&j);if(rc){unlock(m);return rc;}
    if(j->phase!=BPN_VALIDATED){m->busy=0;unlock(m);return BPN_ARGUMENT;}
    rc=recheck(m,j);
    if(!rc)for(unsigned i=0;i<3;i++) {
        struct bpn_return result;rc=call(m,j,i,BPN_ZERO,0,NULL,BPL_PAGE,&result);if(rc)break;
        if(result.outcome!=BPN_CALL_OK){rc=retain(m,j,result.native_error);break;}
    }
    if(!rc)j->phase=BPN_INITIALIZED;
    m->busy=0;unlock(m);return rc;
}
int bpn_copy_private_data(struct bpn_manager *m,const struct bpn_token *t,uint64_t offset,const void *src,size_t n)
{
    if(!valid(m) || !t || !src || !n || offset>BPL_PAGE || n>BPL_PAGE-offset ||
        control_alias(m,t,sizeof *t) || control_alias(m,src,n))return BPN_ARGUMENT;
    lock(m);struct bpn_job *j;int rc=start(m,t,&j);if(rc){unlock(m);return rc;}
    if(j->phase!=BPN_INITIALIZED){rc=BPN_ARGUMENT;goto done;}
    for(unsigned i=0;i<m->protection.cpu_count;i++) {
        struct bpl_range p=m->protection.cpu[i];
        if(overlap((uint64_t)(uintptr_t)src,n,p.base,p.bytes)){rc=BPN_ARGUMENT;goto done;}
    }
    for(unsigned a=0;a<4;a++)for(unsigned b=0;b<3;b++) {
        struct bpn_job *o=&m->jobs[a];uint64_t cpu=o->records[b].view.cpu;
        if(o->phase==BPN_EMPTY || o->phase==BPN_REFS_RELEASED || !cpu)continue;
        if(overlap((uint64_t)(uintptr_t)src,n,cpu,BPL_PAGE)){rc=BPN_ARGUMENT;goto done;}
    }
    rc=recheck(m,j);
    if(!rc){struct bpn_return result;rc=call(m,j,2,BPN_COPY_DATA,offset,src,n,&result);
        if(!rc && result.outcome!=BPN_CALL_OK)rc=retain(m,j,result.native_error);}
done:
    m->busy=0;unlock(m);return rc;
}
int bpn_retire_private(struct bpn_manager *m,const struct bpn_token *t)
{
    if(!valid(m) || !t || control_alias(m,t,sizeof *t))return BPN_ARGUMENT;
    lock(m);struct bpn_job *j;int rc=start(m,t,&j);if(rc){unlock(m);return rc;}
    rc=retire(m,j);m->busy=0;unlock(m);return rc;
}
int bpn_inspect(struct bpn_manager *m,uint32_t slot,struct bpn_snapshot *out)
{
    if(!valid(m) || !out || slot>=4 || control_alias(m,out,sizeof *out))return BPN_ARGUMENT;
    lock(m);const struct bpn_job *j=&m->jobs[slot];
    struct bpn_snapshot s={j->generation,j->sequence,j->phase,m->busy,m->halted,
        j->metadata_bound,j->core_retired,j->call_pending,0,0,0,0,j->first_error};
    for(unsigned i=0;i<3;i++) {const struct bpn_record *r=&j->records[i];
        s.identity_records+=r->identity!=0;s.owned_pairs+=r->owns_pair;
        s.balanced_pairs+=r->pair_balanced;s.resets_returned+=r->reset_returned;}
    *out=s;unlock(m);return BPN_OK;
}
