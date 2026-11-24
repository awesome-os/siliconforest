/* Original bounded metadata-only implementation, numerical source facts cited
 * in README. This does not invoke m1n1 or reproduce its warning-and-skip path. */
#include "mcc_snapshot.h"
static int overlap(const void *a,size_t an,const void *b,size_t bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(an>UINTPTR_MAX-x || bn>UINTPTR_MAX-y)return 1;
    return x<y+bn && y<x+an;
}
static uint32_t word(const uint8_t *p)
{ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static size_t at(unsigned pass,unsigned plane,unsigned slot,unsigned field)
{ return ((((size_t)pass*MCP_PLANES+plane)*MCP_SLOTS+slot)*MCP_FIELDS+field)*4; }
int mcp_plan_read(uint32_t profile,uint32_t index,uint64_t region_bytes,struct mcp_read *out)
{
    if(profile!=MCP_PROFILE_T8103)return MCP_PROFILE;
    if(!out || index>=MCP_READS)return MCP_ARGUMENT;
    unsigned field=index%3,slot=(index/3)%4,plane=(index/12)%4,pass=index/48;
    uint64_t offset=(uint64_t)plane*UINT64_C(0x40000)+UINT64_C(0x6a0)+(uint64_t)slot*0x10+field*4;
    /* Require coverage for the ENTIRE fixed plan before issuing even index0. */
    if(region_bytes<UINT64_C(0xc06dc) || offset>region_bytes || region_bytes-offset<4)return MCP_RANGE;
    *out=(struct mcp_read){offset,pass,plane,slot,field};return MCP_OK;
}
static void add_union(struct mcp_result *r)
{
    for(unsigned i=0;i<MCP_SLOTS;i++)if(r->slot[i].enabled)
        r->excluded[r->union_count++]=r->slot[i].range;
    for(unsigned i=1;i<r->union_count;i++)for(unsigned j=i;j && r->excluded[j].base<r->excluded[j-1].base;j--) {
        struct mcp_range tmp=r->excluded[j];r->excluded[j]=r->excluded[j-1];r->excluded[j-1]=tmp;
    }
    unsigned n=0;
    for(unsigned i=0;i<r->union_count;i++) {
        struct mcp_range next=r->excluded[i];
        if(n && next.base<=r->excluded[n-1].base+r->excluded[n-1].bytes) {
            uint64_t high=next.base+next.bytes,old=r->excluded[n-1].base+r->excluded[n-1].bytes;
            if(high>old)r->excluded[n-1].bytes=high-r->excluded[n-1].base;
        } else r->excluded[n++]=next;
    }
    for(unsigned i=n;i<MCP_SLOTS;i++)r->excluded[i]=(struct mcp_range){0,0};
    r->union_count=n;
}
int mcp_decode(uint32_t profile,const uint8_t *raw,size_t bytes,
               const struct mcp_range *dram,struct mcp_result *out)
{
    if(profile!=MCP_PROFILE_T8103)return MCP_PROFILE;
    if(!raw || !dram || !out || bytes!=MCP_BYTES ||
        overlap(raw,bytes,out,sizeof *out) || overlap(dram,sizeof *dram,out,sizeof *out))return MCP_ARGUMENT;
    if(dram->base!=MCP_DRAM_BASE || !dram->bytes || dram->bytes>MCP_PA_LIMIT-dram->base ||
        (dram->bytes&0xfff))return MCP_RANGE;
    /* Stability is exact even for disabled raw bounds. No retries. */
    for(size_t i=0;i<MCP_BYTES/2;i++)if(raw[i]!=raw[i+MCP_BYTES/2])return MCP_CHANGED;
    for(unsigned plane=1;plane<MCP_PLANES;plane++)for(unsigned slot=0;slot<MCP_SLOTS;slot++)
        for(unsigned field=0;field<MCP_FIELDS;field++)
            if(word(raw+at(0,0,slot,field))!=word(raw+at(0,plane,slot,field)))return MCP_PLANES_DIFFER;
    struct mcp_result staged={0};
    staged.profile=profile;staged.coverage=MCP_FOUR_PLANES_FOUR_TZ_ONLY;
    staged.passes=MCP_PASSES;staged.planes=MCP_PLANES;staged.slots=MCP_SLOTS;staged.dram=*dram;
    for(unsigned i=0;i<MCP_SLOTS;i++) {
        struct mcp_slot *s=&staged.slot[i];
        s->start_word=word(raw+at(0,0,i,MCP_START));s->end_word=word(raw+at(0,0,i,MCP_END));
        s->enable_word=word(raw+at(0,0,i,MCP_ENABLE));s->enabled=s->enable_word!=0;
        if(!s->enabled)continue;
        if(!s->start_word || s->end_word<=s->start_word)return MCP_ENABLED_INVALID;
        uint64_t low=(uint64_t)s->start_word<<12,high=((uint64_t)s->end_word+1)<<12;
        /* Source uses OR, not generic addition. Do not guess whether register
         * values already contain the DRAM prefix; validate the decoded span. */
        if(low>=MCP_PA_LIMIT || high>MCP_PA_LIMIT)return MCP_ENABLED_INVALID;
        low|=MCP_DRAM_BASE;high|=MCP_DRAM_BASE;
        if(low<dram->base || high<=low || high>MCP_PA_LIMIT ||
            high>dram->base+dram->bytes)return MCP_ENABLED_INVALID;
        s->range=(struct mcp_range){low,high-low};staged.enabled_count++;
    }
    add_union(&staged);*out=staged;return MCP_OK;
}
