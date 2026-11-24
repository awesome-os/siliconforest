/* Original bounded copy/consistency coordinator. No platform APIs in this TU. */
#include "policy_reader_internal.h"
static int overlap(const void *a,size_t an,const void *b,size_t bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(an>UINTPTR_MAX-x || bn>UINTPTR_MAX-y)return 1;
    return x<y+bn && y<x+an;
}
static int equal(const uint8_t *a,const uint8_t *b,size_t n)
{ for(size_t i=0;i<n;i++)if(a[i]!=b[i])return 0;return 1; }
static int nonzero(const uint8_t *p,size_t n)
{ for(size_t i=0;i<n;i++)if(p[i])return 1;return 0; }
static void copy(uint8_t *d,const uint8_t *s,size_t n)
{ for(size_t i=0;i<n;i++)d[i]=s[i]; }
static uint64_t u64(const uint8_t *p)
{ uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)p[i]<<(i*8);return v; }
static int view_ok(const uint8_t *p,size_t n,const struct npr_ops *ops,
                   const struct nps_expected *expected,struct npr_workspace *w,struct nps_result *out)
{
    return p && n && !overlap(p,n,ops,sizeof *ops) && !overlap(p,n,expected,sizeof *expected) &&
        !overlap(p,n,w,sizeof *w) && !overlap(p,n,out,sizeof *out);
}
int npr_read_with_ops(const struct npr_ops *operations,const struct nps_expected *expected,
                      struct npr_workspace *w,struct nps_result *out)
{
    if(!operations || !expected || !w || !out ||
        overlap(operations,sizeof *operations,expected,sizeof *expected) ||
        overlap(operations,sizeof *operations,w,sizeof *w) || overlap(operations,sizeof *operations,out,sizeof *out) ||
        overlap(expected,sizeof *expected,w,sizeof *w) || overlap(expected,sizeof *expected,out,sizeof *out) ||
        overlap(w,sizeof *w,out,sizeof *out))return NPR_ARGUMENT;
    struct npr_ops ops=*operations;
    if(!ops.begin || !ops.boot || !ops.view || !ops.finish || !nonzero(expected->nonce,NPS_NONCE_BYTES))return NPR_ARGUMENT;
    for(unsigned i=0;i<NPS_IDENTITIES;i++)if(!nonzero(expected->sha256[i],NPS_SHA_BYTES))return NPR_ARGUMENT;
    w->expected=*expected;
    struct npr_boot_view boot={0},again={0};
    struct npr_view views[3]={{0}},next={0};
    int status=ops.begin(ops.context);
    if(status)goto finished;
    status=ops.boot(ops.context,&boot);if(status)goto finished;
    if(boot.initialized!=1 || !boot.object || !boot.bytes) { status=NPR_NOT_READY;goto finished; }
    if(boot.count!=NPR_COMMON_PREFIX_BYTES || !view_ok(boot.bytes,boot.count,operations,expected,w,out)) { status=NPR_FORMAT;goto finished; }
    for(unsigned i=0;i<3;i++) {
        status=ops.view(ops.context,(enum npr_source_id)i,&views[i]);if(status)goto finished;
        size_t n=views[i].count;
        if(!views[i].object || (i<2?n!=8:(n<NPS_HEADER_BYTES || n>NPS_WIRE_MAX)) ||
            !view_ok(views[i].bytes,n,operations,expected,w,out) || overlap(views[i].bytes,n,boot.bytes,boot.count)) { status=NPR_FORMAT;goto finished; }
        for(unsigned j=0;j<i;j++)if(overlap(views[i].bytes,n,views[j].bytes,views[j].count)) { status=NPR_FORMAT;goto finished; }
    }
    copy(w->common_prefix,boot.bytes,NPR_COMMON_PREFIX_BYTES);
    copy(w->dram_bytes,views[0].bytes,8);copy(w->dram_bytes+8,views[1].bytes,8);
    copy(w->facts,views[2].bytes,views[2].count);
    if(nps_decode(w->facts,views[2].count,&w->expected,&w->decoder,&w->staged)!=NPS_OK) { status=NPR_FACTS;goto finished; }
    uint64_t phys=u64(w->common_prefix+16),mem=u64(w->common_prefix+24),top=u64(w->common_prefix+32);
    if(w->common_prefix[0]!=2 || w->common_prefix[1]!=0 || w->common_prefix[2]!=2 || w->common_prefix[3]!=0 ||
        w->staged.dram.base!=u64(w->dram_bytes) || w->staged.dram.bytes!=u64(w->dram_bytes+8) ||
        w->staged.envelope.base!=phys || w->staged.envelope.bytes!=mem ||
        w->staged.retained_boot.base!=phys || top<=phys || top>w->staged.retained_boot.base+w->staged.retained_boot.bytes) { status=NPR_ENVELOPE;goto finished; }
    status=ops.boot(ops.context,&again);if(status)goto finished;
    if(again.initialized!=boot.initialized || again.bytes!=boot.bytes || again.count!=boot.count || again.object!=boot.object ||
        !equal(again.bytes,w->common_prefix,NPR_COMMON_PREFIX_BYTES)) { status=NPR_CHANGED;goto finished; }
    for(unsigned i=0;i<3;i++) {
        status=ops.view(ops.context,(enum npr_source_id)i,&next);if(status)goto finished;
        const uint8_t *saved=i<2?w->dram_bytes+i*8:w->facts;
        if(next.bytes!=views[i].bytes || next.count!=views[i].count || next.object!=views[i].object ||
            !equal(next.bytes,saved,next.count)) { status=NPR_CHANGED;goto finished; }
    }
    if(!equal((const uint8_t *)expected,(const uint8_t *)&w->expected,sizeof *expected))status=NPR_CHANGED;
finished:
    /* Cached borrowed pointers are stack-only; finish releases all actual refs.
     * Cleanup uncertainty disqualifies even an otherwise-valid staged result. */
    if(ops.finish(ops.context)!=NPR_OK)return NPR_CLEANUP;
    if(status!=NPR_OK)return status;
    *out=w->staged;return NPR_OK;
}
