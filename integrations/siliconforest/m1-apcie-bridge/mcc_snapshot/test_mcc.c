#include "mcc_snapshot.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "MCC snapshot tests require assertions"
#endif
static uint8_t raw[MCP_BYTES];
static const struct mcp_range dram={MCP_DRAM_BASE,UINT64_C(0x400000000)};
static unsigned checks;
#define CHECK(x) do{checks++;assert(x);}while(0)
#define RC(x,want) do{int got=(x);checks++;if(got!=(want))fprintf(stderr,"line%d got%d expected%d\n",__LINE__,got,(want));assert(got==(want));}while(0)
static void put(size_t offset,uint32_t value)
{for(unsigned i=0;i<4;i++)raw[offset+i]=(uint8_t)(value>>(i*8));}
static size_t index_of(unsigned pass,unsigned plane,unsigned slot,unsigned field)
{return ((pass*48u)+(plane*12u)+(slot*3u)+field)*4u;}
static void slot(unsigned number,uint32_t start,uint32_t end,uint32_t enable)
{
    for(unsigned p=0;p<2;p++)for(unsigned c=0;c<4;c++) {
        put(index_of(p,c,number,0),start);put(index_of(p,c,number,1),end);put(index_of(p,c,number,2),enable);
    }
}
static void refuse(uint32_t profile,const uint8_t *bytes,size_t size,const struct mcp_range *d,int want)
{
    struct mcp_result out,before;memset(&out,0xa5,sizeof out);before=out;
    RC(mcp_decode(profile,bytes,size,d,&out),want);CHECK(!memcmp(&out,&before,sizeof out));
}
static void plans(void)
{
    const uint64_t planes[]={0,0x40000,0x80000,0xc0000};
    const uint64_t slots[]={0x6a0,0x6b0,0x6c0,0x6d0};
    const uint64_t fields[]={0,4,8};unsigned index=0;
    for(unsigned p=0;p<2;p++)for(unsigned c=0;c<4;c++)for(unsigned s=0;s<4;s++)for(unsigned f=0;f<3;f++) {
        struct mcp_read r;RC(mcp_plan_read(1,index++,0xc06dc,&r),MCP_OK);
        CHECK(r.pass==p && r.plane==c && r.slot==s && r.field==f);
        CHECK(r.offset==planes[c]+slots[s]+fields[f]);
    }
    CHECK(index==96);struct mcp_read r,before;memset(&r,0x5a,sizeof r);before=r;
    RC(mcp_plan_read(1,0,0xc06db,&r),MCP_RANGE);CHECK(!memcmp(&r,&before,sizeof r));
    RC(mcp_plan_read(1,0,0x6a4,&r),MCP_RANGE);CHECK(!memcmp(&r,&before,sizeof r));
    RC(mcp_plan_read(1,96,UINT64_MAX,&r),MCP_ARGUMENT);CHECK(!memcmp(&r,&before,sizeof r));
    RC(mcp_plan_read(1,UINT32_MAX,UINT64_MAX,&r),MCP_ARGUMENT);
    RC(mcp_plan_read(0,0,UINT64_MAX,&r),MCP_PROFILE);CHECK(!memcmp(&r,&before,sizeof r));
    RC(mcp_plan_read(1,0,UINT64_MAX,NULL),MCP_ARGUMENT);
}
static void healthy(void)
{
    struct mcp_result r;memset(raw,0,sizeof raw);
    slot(0,10,12,0x80000000);slot(1,12,14,1);slot(2,17,18,2);slot(3,UINT32_MAX,UINT32_MAX-1,0);
    RC(mcp_decode(1,raw,sizeof raw,&dram,&r),MCP_OK);
    CHECK(r.profile==1 && r.coverage==MCP_FOUR_PLANES_FOUR_TZ_ONLY && r.passes==2 && r.planes==4 && r.slots==4);
    CHECK(r.enabled_count==3 && r.union_count==2);
    CHECK(r.excluded[0].base==MCP_DRAM_BASE+0xa000 && r.excluded[0].bytes==0x5000);
    CHECK(r.excluded[1].base==MCP_DRAM_BASE+0x11000 && r.excluded[1].bytes==0x2000);
    CHECK(!r.slot[3].enabled && r.slot[3].start_word==UINT32_MAX && !r.slot[3].range.bytes);
    CHECK(!r.excluded[2].base && !r.excluded[3].bytes);
    memset(raw,0,sizeof raw);slot(0,0x800100,0x800101,0x80);
    RC(mcp_decode(1,raw,sizeof raw,&dram,&r),MCP_OK);
    CHECK(r.enabled_count==1 && r.slot[0].range.base==MCP_DRAM_BASE+0x100000 && r.slot[0].range.bytes==0x2000);
    slot(0,0xbffffe,0xbfffff,1);
    RC(mcp_decode(1,raw,sizeof raw,&dram,&r),MCP_OK);
    CHECK(r.slot[0].range.base==MCP_DRAM_BASE+dram.bytes-0x2000 && r.slot[0].range.bytes==0x2000);
    memset(raw,0,sizeof raw);RC(mcp_decode(1,raw,sizeof raw,&dram,&r),MCP_OK);
    CHECK(!r.enabled_count && !r.union_count && r.coverage==MCP_FOUR_PLANES_FOUR_TZ_ONLY);
}
static void invalid(void)
{
    memset(raw,0,sizeof raw);
    refuse(0,raw,sizeof raw,&dram,MCP_PROFILE);refuse(1,NULL,sizeof raw,&dram,MCP_ARGUMENT);
    refuse(1,raw,0,&dram,MCP_ARGUMENT);refuse(1,raw,80,&dram,MCP_ARGUMENT);
    refuse(1,raw,sizeof raw-1,&dram,MCP_ARGUMENT);refuse(1,raw,sizeof raw+1,&dram,MCP_ARGUMENT);
    refuse(1,raw,sizeof raw,NULL,MCP_ARGUMENT);
    struct mcp_range d=dram;d.base=0;refuse(1,raw,sizeof raw,&d,MCP_RANGE);
    d=dram;d.bytes=0;refuse(1,raw,sizeof raw,&d,MCP_RANGE);
    d=dram;d.bytes=MCP_PA_LIMIT;refuse(1,raw,sizeof raw,&d,MCP_RANGE);
    d=dram;d.bytes++;refuse(1,raw,sizeof raw,&d,MCP_RANGE);
    const uint32_t bad[][2]={{0,3},{3,3},{4,2},{1,UINT32_MAX},{0x400001,0x400002}};
    for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++) {
        memset(raw,0,sizeof raw);slot(0,bad[i][0],bad[i][1],1);
        refuse(1,raw,sizeof raw,&dram,MCP_ENABLED_INVALID);
    }
    memset(raw,0,sizeof raw);
    for(size_t i=MCP_BYTES/2;i<MCP_BYTES;i++) {
        raw[i]=0x80;refuse(1,raw,sizeof raw,&dram,MCP_CHANGED);raw[i]=0;
    }
    for(unsigned plane=1;plane<4;plane++)for(unsigned s=0;s<4;s++)for(unsigned f=0;f<3;f++) {
        for(unsigned p=0;p<2;p++)put(index_of(p,plane,s,f),1);
        refuse(1,raw,sizeof raw,&dram,MCP_PLANES_DIFFER);
        for(unsigned p=0;p<2;p++)put(index_of(p,plane,s,f),0);
    }
    union {struct mcp_result result;uint8_t bytes[MCP_BYTES+sizeof(struct mcp_result)];} alias;
    memset(&alias,0,sizeof alias);uint8_t before[sizeof alias];memcpy(before,&alias,sizeof alias);
    RC(mcp_decode(1,alias.bytes,MCP_BYTES,&dram,&alias.result),MCP_ARGUMENT);
    CHECK(!memcmp(before,&alias,sizeof alias));
    struct mcp_result r,saved;memset(&r,0xa5,sizeof r);r.dram=dram;saved=r;
    RC(mcp_decode(1,raw,sizeof raw,&r.dram,&r),MCP_ARGUMENT);CHECK(!memcmp(&r,&saved,sizeof r));
    RC(mcp_decode(1,raw,sizeof raw,&dram,NULL),MCP_ARGUMENT);
}
static uint32_t next(uint32_t *s){*s=*s*1664525u+1013904223u;return *s;}
static void union_oracle(void)
{
    uint32_t seed=12345;
    for(unsigned trial=0;trial<1024;trial++) {
        uint8_t expected[256]={0},actual[256]={0};unsigned count=0;
        memset(raw,0,sizeof raw);
        for(unsigned i=0;i<4;i++) {
            uint32_t start=1+next(&seed)%128,end=start+1+next(&seed)%8,enable=next(&seed)&3u;
            slot(i,start,end,enable);
            if(enable){count++;for(uint32_t j=start;j<=end;j++)expected[j]=1;}
        }
        struct mcp_result r;RC(mcp_decode(1,raw,sizeof raw,&dram,&r),MCP_OK);CHECK(r.enabled_count==count);
        for(unsigned i=0;i<r.union_count;i++) {
            struct mcp_range v=r.excluded[i];
            CHECK(v.base>=MCP_DRAM_BASE && !((v.base|v.bytes)&0xfff) && v.bytes);
            unsigned low=(unsigned)((v.base-MCP_DRAM_BASE)>>12),n=(unsigned)(v.bytes>>12);
            CHECK(low<256 && n<=256-low);
            if(i)CHECK(r.excluded[i-1].base+r.excluded[i-1].bytes<v.base);
            for(unsigned j=low;j<low+n;j++)actual[j]=1;
        }
        CHECK(!memcmp(expected,actual,sizeof expected));
        for(unsigned i=r.union_count;i<4;i++)CHECK(!r.excluded[i].base && !r.excluded[i].bytes);
    }
}
int main(void)
{
    plans();healthy();invalid();union_oracle();
    printf("MCC metadata checks=%u; no hardware/read backend or complete-policy claim\n",checks);return 0;
}
