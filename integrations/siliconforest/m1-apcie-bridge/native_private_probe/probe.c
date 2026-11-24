/* Original bounded private lifecycle workload; no effect commands/readback. */
#include "probe.h"
#include <string.h>
_Static_assert(BNO_INITIALIZED==3 && BNO_REFS_RELEASED==6,"frozen public phase ABI");
_Static_assert(sizeof(struct nps_expected)==216 && offsetof(struct nps_expected,nonce)==0 &&
               offsetof(struct nps_expected,sha256)==24,"exact chosen identity layout");
static void issue(struct bcp_record *r,unsigned op,unsigned stage)
{ bcp_put(r,BCP_STAGE_AT,stage);bcp_put(r,BCP_ATTEMPTS_AT,bcp_word(r,BCP_ATTEMPTS_AT)|(1u<<op)); }
static void result(struct bcp_record *r,unsigned op,int rc)
{
    bcp_put(r,BCP_STATUS_AT+op*4,(uint32_t)rc);
    if(rc && !bcp_word(r,BCP_ERROR_AT))bcp_put(r,BCP_ERROR_AT,(uint32_t)rc);
}
static void snapshot(struct bcp_record *r,unsigned at,const struct bno_snapshot *s)
{
    uint32_t words[14]={s->phase,s->busy,s->halted,s->metadata_bound,s->core_retired,
       s->call_pending,s->identity_records,s->owned_pairs,s->balanced_pairs,s->resets_returned,
       (uint32_t)s->first_error,s->effect_count,s->effect_pending,s->ever_resources};
    for(unsigned i=0;i<14;i++)bcp_put(r,at+4*i,words[i]);
}
static void shape_error(struct bcp_record *r)
{ if(!bcp_word(r,BCP_ERROR_AT))bcp_put(r,BCP_ERROR_AT,(uint32_t)BCP_SNAPSHOT); }
void bcp_run(const struct bcp_ops *operations,struct bcp_workspace *w,struct bcp_record *r)
{
    /* All objects/ops are trusted private driver storage, not caller addresses.
     * Missing/failed native paths never get substituted by successful callbacks. */
    const struct bcp_ops ops=*operations;
    issue(r,BCP_LABEL_OP,BCP_LABELS);
    int rc=ops.labels(ops.context,&w->expected.identity);result(r,BCP_LABEL_OP,rc);if(rc)goto done;
    memcpy(r->bytes+BCP_IDENTITY_AT,&w->expected.identity,216);
    w->expected.platform_domain=1;w->expected.dart_domain=2;w->expected.irq_domain=3;
    w->expected.profile=NPP_VM_PRIVATE_PROFILE;w->expected.reserved=0;
    issue(r,BCP_CONFIGURE_OP,BCP_CONFIGURE);
    rc=ops.configure(ops.context,&w->expected,&w->policy,&w->configured);result(r,BCP_CONFIGURE_OP,rc);if(rc)goto done;
    bcp_put(r,BCP_CONFIG_AT,w->configured.version);bcp_put(r,BCP_CONFIG_AT+4,w->configured.profile);
    bcp_put(r,BCP_CONFIG_AT+8,w->configured.ram_count);bcp_put(r,BCP_CONFIG_AT+12,w->configured.reserved_count);
    bcp_put(r,BCP_CONFIG_AT+16,w->configured.control_pages);bcp_put(r,BCP_CONFIG_AT+20,w->configured.queries);
    const struct bno_request request={0x100,0,1}; /* synthetic metadata, no identity */
    issue(r,BCP_ACQUIRE_OP,BCP_ACQUIRE);
    rc=ops.acquire(ops.context,&request,UINT64_C(0x100000),3,&w->token);result(r,BCP_ACQUIRE_OP,rc);if(rc)goto done;
    issue(r,BCP_ZERO_OP,BCP_ZERO);rc=ops.zero(ops.context,&w->token);result(r,BCP_ZERO_OP,rc);
    if(!rc) {
        issue(r,BCP_COPY_OP,BCP_COPY);bcp_put(r,BCP_COPY_BYTES_AT,16);
        rc=ops.copy(ops.context,&w->token,0,w->expected.identity.nonce,16);result(r,BCP_COPY_OP,rc);
    }
    issue(r,BCP_PRE_OP,BCP_PRE);rc=ops.inspect(ops.context,w->token.slot,&w->snapshot);result(r,BCP_PRE_OP,rc);
    if(!rc) {snapshot(r,BCP_PRE_AT,&w->snapshot);if(!bcp_snapshot_ok(r,BCP_PRE_AT,0))shape_error(r);}
    /* One public guarded retirement after rc0 acquisition, even if a later
     * step failed. Halted/quarantined state refuses internally; no manual free. */
    issue(r,BCP_RETIRE_OP,BCP_RETIRE);rc=ops.retire(ops.context,&w->token);result(r,BCP_RETIRE_OP,rc);
    issue(r,BCP_POST_OP,BCP_POST);rc=ops.inspect(ops.context,w->token.slot,&w->snapshot);result(r,BCP_POST_OP,rc);
    if(!rc) {snapshot(r,BCP_POST_AT,&w->snapshot);if(!bcp_snapshot_ok(r,BCP_POST_AT,1))shape_error(r);}
done:
    bcp_put(r,BCP_STAGE_AT,BCP_DONE);
    bcp_put(r,BCP_RESULT_AT,bcp_word(r,BCP_ERROR_AT)?BCP_FAILED:BCP_SELF_REPORT);
    /* Final validator ensures no callback omission/malformed result promotes
     * success, even if a trusted test callee falsely returned rc0. */
    if(!bcp_word(r,BCP_ERROR_AT) && bcp_validate(r->bytes,BCP_BYTES,w->expected.identity.nonce)!=0) {
        shape_error(r);bcp_put(r,BCP_RESULT_AT,BCP_FAILED);
    }
}
