/* Original metadata-only implementation of WIRE_DESIGN.md. */
#include "policy_snapshot.h"
static int overlap(const void *a,size_t an,const void *b,size_t bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(an>UINTPTR_MAX-x || bn>UINTPTR_MAX-y)return 1;
    return x<y+bn && y<x+an;
}
static uint32_t u16(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8); }
static uint32_t u32(const uint8_t *p) { return u16(p)|(u16(p+2)<<16); }
static uint64_t u64(const uint8_t *p) { return (uint64_t)u32(p)|((uint64_t)u32(p+4)<<32); }
static int zero(const uint8_t *p,size_t n) { for(size_t i=0;i<n;i++)if(p[i])return 0;return 1; }
static int equal(const uint8_t *a,const uint8_t *b,size_t n) { for(size_t i=0;i<n;i++)if(a[i]!=b[i])return 0;return 1; }
static void copy(uint8_t *d,const uint8_t *s,size_t n) { for(size_t i=0;i<n;i++)d[i]=s[i]; }
static struct mcp_range range_at(const uint8_t *p) { return (struct mcp_range){u64(p),u64(p+8)}; }
static int valid_range(struct mcp_range r) { return r.bytes && r.base<MCP_PA_LIMIT && r.bytes<=MCP_PA_LIMIT-r.base; }
static int contains(struct mcp_range outer,struct mcp_range inner)
{ return inner.base>=outer.base && inner.base+inner.bytes<=outer.base+outer.bytes; }
static int intersects(struct mcp_range a,struct mcp_range b)
{ return a.base<b.base+b.bytes && b.base<a.base+a.bytes; }
static int aligned(struct mcp_range r,uint64_t mask) { return !((r.base|r.bytes)&mask); }
static int same_range(struct mcp_range a,struct mcp_range b) { return a.base==b.base && a.bytes==b.bytes; }
int nps_decode(const uint8_t *raw,size_t bytes,const struct nps_expected *expected,
               struct nps_workspace *workspace,struct nps_result *out)
{
    if(!raw || !expected || !workspace || !out || bytes<NPS_HEADER_BYTES || bytes>NPS_WIRE_MAX ||
        overlap(raw,bytes,expected,sizeof *expected) || overlap(raw,bytes,workspace,sizeof *workspace) ||
        overlap(raw,bytes,out,sizeof *out) || overlap(expected,sizeof *expected,workspace,sizeof *workspace) ||
        overlap(expected,sizeof *expected,out,sizeof *out) || overlap(workspace,sizeof *workspace,out,sizeof *out))return NPS_ARGUMENT;
    if(!equal(raw,(const uint8_t *)"BCMFCT01",8) || u32(raw+8)!=1 || u32(raw+12)!=bytes ||
        u32(raw+16)!=1 || u32(raw+20)!=14 || u32(raw+24)!=36 || u32(raw+28)!=1 ||
        u32(raw+40)!=NPS_SOURCES || u32(raw+44)!=MCP_BYTES || u32(raw+48)!=MCP_FOUR_PLANES_FOUR_TZ_ONLY ||
        !zero(raw+52,12) || !zero(raw+280,40) || !zero(raw+1168,112))return NPS_FORMAT;
    uint32_t nr=u32(raw+32),n=u32(raw+36);
    if(!nr || nr>NPS_RAM_MAX || n>NPS_RECORDS || bytes!=NPS_HEADER_BYTES+(size_t)n*NPS_RECORD_BYTES)return NPS_CAPACITY;
    if(zero(expected->nonce,NPS_NONCE_BYTES) || !equal(raw+64,expected->nonce,NPS_NONCE_BYTES))return NPS_IDENTITY;
    for(unsigned i=0;i<NPS_IDENTITIES;i++)
        if(zero(expected->sha256[i],NPS_SHA_BYTES) || !equal(raw+88+i*NPS_SHA_BYTES,expected->sha256[i],NPS_SHA_BYTES))return NPS_IDENTITY;
    struct nps_result *r=&workspace->staged;
    for(size_t i=0;i<sizeof *r;i++)((uint8_t *)r)[i]=0;
    r->version=1;r->board_profile=1;r->page_shift=14;r->pa_bits=36;r->wire_profile=1;
    r->coverage=MCP_FOUR_PLANES_FOUR_TZ_ONLY;r->ram_count=nr;r->record_count=n;r->identity=*expected;
    r->dram=range_at(raw+320);r->envelope=range_at(raw+336);r->retained_boot=range_at(raw+352);
    if(!valid_range(r->dram) || r->dram.base!=MCP_DRAM_BASE || !aligned(r->dram,0xfff) ||
        !valid_range(r->envelope) || !aligned(r->envelope,0x3fff) || !contains(r->dram,r->envelope) ||
        !valid_range(r->retained_boot) || !aligned(r->retained_boot,0x3fff))return NPS_RANGE;
    unsigned boot_contained=0;
    for(unsigned i=0;i<NPS_RAM_MAX;i++) {
        struct mcp_range next=range_at(raw+368+i*16);
        if(i>=nr) { if(next.base || next.bytes)return NPS_FORMAT;continue; }
        if(!valid_range(next) || !aligned(next,0x3fff) || !contains(r->envelope,next) ||
            (i && next.base<r->ram[i-1].base+r->ram[i-1].bytes))return NPS_RANGE;
        r->ram[i]=next;if(contains(next,r->retained_boot))boot_contained=1;
    }
    if(!boot_contained)return NPS_RANGE;
    uint32_t total=0;
    for(unsigned i=0;i<NPS_SOURCES;i++) {
        const uint8_t *s=raw+496+i*48;
        uint32_t count=u32(s+4),status=u16(s+2);
        if(u16(s)!=i+1 || count>NPS_RECORDS || total>NPS_RECORDS-count ||
            (status!=1 && status!=2) || (status==1)!=(count==0) ||
            zero(s+8,NPS_SHA_BYTES) || !zero(s+40,8))return NPS_SOURCE;
        r->source[i].tag=i+1;r->source[i].status=status;r->source[i].count=count;
        copy(r->source[i].declared_source_sha256,s+8,NPS_SHA_BYTES);total+=count;
    }
    if(total!=n || r->source[NPS_FRAMEBUFFER-1].count!=1 || r->source[NPS_RETAINED_BOOT-1].count!=1)return NPS_SOURCE;
    copy(r->raw_mcc,raw+784,MCP_BYTES);
    if(mcp_decode(MCP_PROFILE_T8103,r->raw_mcc,MCP_BYTES,&r->dram,&r->mcc)!=MCP_OK ||
        r->source[NPS_MCC_TZ-1].count!=r->mcc.enabled_count)return NPS_MCC;
    unsigned at=0,sorted_count=0;
    for(unsigned source=0;source<NPS_SOURCES;source++) {
        unsigned slot=0;
        for(unsigned j=0;j<r->source[source].count;j++,at++) {
            const uint8_t *p=raw+NPS_HEADER_BYTES+(size_t)at*NPS_RECORD_BYTES;
            struct nps_record *rec=&r->record[at];
            rec->tag=u32(p);rec->ordinal=u32(p+4);rec->flags=u32(p+8);rec->auxiliary=u32(p+12);rec->range=range_at(p+16);
            if(rec->tag!=source+1 || !valid_range(rec->range) ||
                (rec->tag!=NPS_PMAP_IO && (rec->flags || rec->auxiliary)))return NPS_SOURCE;
            if(rec->tag==NPS_MCC_TZ) {
                while(slot<MCP_SLOTS && !r->mcc.slot[slot].enabled)slot++;
                if(slot>=MCP_SLOTS || rec->ordinal!=slot || !same_range(rec->range,r->mcc.slot[slot].range))return NPS_MCC;
                slot++;
            } else if(rec->ordinal!=j)return NPS_SOURCE;
            if(rec->tag==NPS_RETAINED_BOOT && !same_range(rec->range,r->retained_boot))return NPS_SOURCE;
            if(rec->tag==NPS_REMAP_SOURCE_ONLY)continue;
            for(unsigned k=0;k<nr;k++)if(intersects(rec->range,r->ram[k])) {
                workspace->sorted[sorted_count++]=rec->range;break;
            }
        }
    }
    for(unsigned i=1;i<sorted_count;i++)for(unsigned j=i;j && workspace->sorted[j].base<workspace->sorted[j-1].base;j--) {
        struct mcp_range tmp=workspace->sorted[j];workspace->sorted[j]=workspace->sorted[j-1];workspace->sorted[j-1]=tmp;
    }
    for(unsigned i=0;i<sorted_count;i++) {
        struct mcp_range next=workspace->sorted[i];unsigned count=r->union_count;
        if(count && next.base<=r->relevant_union[count-1].base+r->relevant_union[count-1].bytes) {
            uint64_t high=next.base+next.bytes,old=r->relevant_union[count-1].base+r->relevant_union[count-1].bytes;
            if(high>old)r->relevant_union[count-1].bytes=high-r->relevant_union[count-1].base;
        } else {
            if(count==NPS_UNION_MAX)return NPS_CAPACITY;
            r->relevant_union[count]=next;r->union_count=count+1;
        }
    }
    *out=*r;return NPS_OK;
}
