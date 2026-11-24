/* Original host-only synthetic metadata tests; no native API or physical read. */
#ifdef NDEBUG
#error tests require assertions
#endif
#include "policy_snapshot.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t raw[NPS_WIRE_MAX],saved[NPS_WIRE_MAX];
static struct nps_expected expected,original_expected;
static struct nps_workspace workspace;
static struct nps_result out,before;
static unsigned checks;
static void put16(uint8_t *p,uint32_t x) { p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8); }
static void put32(uint8_t *p,uint32_t x) { for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(x>>(i*8)); }
static void put64(uint8_t *p,uint64_t x) { for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(x>>(i*8)); }
static void span(unsigned at,uint64_t base,uint64_t size) { put64(raw+at,base);put64(raw+at+8,size); }
static void record(unsigned i,unsigned tag,unsigned ordinal,uint64_t base,uint64_t bytes)
{
    unsigned at=NPS_HEADER_BYTES+i*NPS_RECORD_BYTES;
    put32(raw+at,tag);put32(raw+at+4,ordinal);span(at+16,base,bytes);
}
static size_t fixture(unsigned pmap_count,int pmap_relevant)
{
    memset(raw,0,sizeof raw);memset(&expected,0,sizeof expected);
    memcpy(raw,"BCMFCT01",8);unsigned count=6+pmap_count;
    uint32_t header[]={1,NPS_HEADER_BYTES+count*NPS_RECORD_BYTES,1,14,36,1,1,count,6,384,1};
    for(unsigned i=0;i<11;i++)put32(raw+8+i*4,header[i]);
    for(unsigned i=0;i<NPS_NONCE_BYTES;i++)expected.nonce[i]=(uint8_t)(i+1);
    for(unsigned i=0;i<NPS_IDENTITIES;i++)memset(expected.sha256[i],(int)i+1,NPS_SHA_BYTES);
    memcpy(raw+64,expected.nonce,NPS_NONCE_BYTES);memcpy(raw+88,expected.sha256,sizeof expected.sha256);
    uint64_t low=MCP_DRAM_BASE+0x20000000;
    span(320,MCP_DRAM_BASE,UINT64_C(0x400000000));span(336,low,0x10000000);
    span(352,low+0x100000,0x40000);span(368,low,0x10000000);
    unsigned counts[]={2,pmap_count,1,1,1,1};
    for(unsigned i=0;i<6;i++) {
        unsigned at=496+i*48;put16(raw+at,i+1);put16(raw+at+2,counts[i]?2:1);put32(raw+at+4,counts[i]);
        memset(raw+at+8,(int)i+17,32);
    }
    uint32_t words[]={0x100000,0x100007,1,0,0,0,0x100100,0x100103,2,0,0,0};
    for(unsigned plane_pass=0;plane_pass<8;plane_pass++)for(unsigned i=0;i<12;i++)
        put32(raw+784+(plane_pass*12+i)*4,words[i]);
    record(0,1,0,MCP_DRAM_BASE|UINT64_C(0x100000000),0x8000);
    record(1,1,2,MCP_DRAM_BASE|UINT64_C(0x100100000),0x4000);
    unsigned at=2;
    for(unsigned i=0;i<pmap_count;i++)record(at++,2,i,pmap_relevant?low+i*0x8000:UINT64_C(0x600000000)+i*4096,4096);
    record(at++,3,0,MCP_DRAM_BASE+UINT64_C(0x300000000),0x200000);
    record(at++,4,0,UINT64_C(0x690000000),4096);
    record(at++,5,0,low+0x80000,0x4000);
    record(at++,6,0,low+0x100000,0x40000);assert(at==count);
    original_expected=expected;memcpy(saved,raw,sizeof raw);
    return NPS_HEADER_BYTES+count*NPS_RECORD_BYTES;
}
static void result(int ok,size_t bytes)
{
    memset(&out,0xa5,sizeof out);before=out;
    int status=nps_decode(raw,bytes,&expected,&workspace,&out);checks++;
    assert(ok?status==NPS_OK:status!=NPS_OK);
    if(!ok)assert(memcmp(&out,&before,sizeof out)==0);
}
static void restored_bad(size_t bytes,unsigned at)
{ memcpy(raw,saved,sizeof raw);raw[at]^=1;result(0,bytes); }
int main(void)
{
    size_t n=fixture(1,0);result(1,n);assert(out.record_count==7 && out.union_count==1);
    assert(out.ram_count==1 && out.source[4].count==1 && out.mcc.enabled_count==2);
    assert(out.mcc.slot[2].enable_word==2 && out.mcc.slot[2].range.bytes==0x4000);
    assert(memcmp(out.raw_mcc,raw+784,384)==0);
    for(unsigned at=52;at<64;at++)restored_bad(n,at);
    for(unsigned at=280;at<320;at++)restored_bad(n,at);
    for(unsigned at=1168;at<1280;at++)restored_bad(n,at);
    for(unsigned at=384;at<496;at++)restored_bad(n,at);
    for(unsigned source=0;source<6;source++)for(unsigned j=40;j<48;j++)restored_bad(n,496+source*48+j);
    for(unsigned at=0;at<52;at+=4)restored_bad(n,at);
    for(unsigned at=64;at<280;at++)restored_bad(n,at);
    for(unsigned at=784;at<1168;at++)restored_bad(n,at);
    for(unsigned at=0;at<NPS_NONCE_BYTES;at++) {
        memcpy(raw,saved,sizeof raw);expected=original_expected;expected.nonce[at]^=1;result(0,n);
    }
    for(unsigned at=0;at<NPS_IDENTITIES;at++) {
        expected=original_expected;memset(expected.sha256[at],0,32);result(0,n);
    }
    expected=original_expected;memcpy(raw,saved,sizeof raw);
    result(0,n-1);result(0,n+1);result(0,80);result(0,0);
    for(unsigned source=0;source<6;source++) {
        for(unsigned j=0;j<8;j++)restored_bad(n,496+source*48+j);
        memcpy(raw,saved,sizeof raw);memset(raw+496+source*48+8,0,32);result(0,n);
        memcpy(raw,saved,sizeof raw);put16(raw+496+source*48+2,1);result(0,n);
    }
    for(unsigned rec=0;rec<7;rec++) {
        restored_bad(n,1280+rec*32);restored_bad(n,1280+rec*32+4);
        if(rec!=2)restored_bad(n,1280+rec*32+8);
        memcpy(raw,saved,sizeof raw);span(1280+rec*32+16,MCP_PA_LIMIT-1,2);result(0,n);
        memcpy(raw,saved,sizeof raw);span(1280+rec*32+16,0,0);result(0,n);
    }
    memcpy(raw,saved,sizeof raw);put32(raw+1280+4,1);result(0,n); /* sparse MCC ordinal */
    memcpy(raw,saved,sizeof raw);put64(raw+1280+24,4096);result(0,n); /* MCC exact extent */
    memcpy(raw,saved,sizeof raw);span(1280+6*32+16,MCP_DRAM_BASE,0x4000);result(0,n);
    memcpy(raw,saved,sizeof raw);span(352,MCP_DRAM_BASE,0x4000);result(0,n);
    memcpy(raw,saved,sizeof raw);put64(raw+336,MCP_DRAM_BASE-0x4000);result(0,n);
    memcpy(raw,saved,sizeof raw);put64(raw+320,MCP_DRAM_BASE+0x4000);result(0,n);
    memcpy(raw,saved,sizeof raw);put64(raw+376,0x10000001);result(0,n);
    memcpy(raw,saved,sizeof raw);span(368,MCP_DRAM_BASE+0x1fffc000,0x10000000);result(0,n);
    memcpy(raw,saved,sizeof raw);put32(raw+32,2);span(368,MCP_DRAM_BASE+0x20000000,0x120000);
    span(384,MCP_DRAM_BASE+0x20120000,0xfee0000);result(0,n); /* boot crosses adjacent originals */
    n=fixture(7,1);result(1,n);assert(out.union_count==8);
    n=fixture(8,1);result(0,n);
    n=fixture(250,0);result(1,n);assert(n==NPS_WIRE_MAX && out.record_count==256);
    n=fixture(0,0);result(1,n);assert(out.source[1].status==1);
    /* Source-only remap must not become a physical exclusion. */
    n=fixture(1,0);span(1280+5*32+16,MCP_DRAM_BASE+0x20000000,0x10000000);result(1,n);assert(out.union_count==1);
    /* Full overlapping source interval retained, never clipped at RAM boundary. */
    span(1280+2*32+16,MCP_DRAM_BASE+0x1ffff000,8192);result(1,n);
    assert(out.relevant_union[0].base==MCP_DRAM_BASE+0x1ffff000 && out.relevant_union[0].bytes==8192);
    /* All API alias pairs and wrapping pointer spans refuse before dereference. */
    n=fixture(1,0);memset(&out,0xa5,sizeof out);before=out;
    assert(nps_decode(raw,n,&expected,&workspace,(struct nps_result *)(void *)raw)==NPS_ARGUMENT);
    assert(nps_decode(raw,n,(struct nps_expected *)(void *)raw,&workspace,&out)==NPS_ARGUMENT);
    assert(nps_decode(raw,n,&expected,(struct nps_workspace *)(void *)raw,&out)==NPS_ARGUMENT);
    assert(nps_decode(raw,n,&expected,&workspace,&workspace.staged)==NPS_ARGUMENT);
    assert(nps_decode(raw,n,(struct nps_expected *)(void *)&workspace,&workspace,&out)==NPS_ARGUMENT);
    assert(nps_decode(raw,n,(struct nps_expected *)(void *)&out,&workspace,&out)==NPS_ARGUMENT);
    assert(nps_decode((const uint8_t *)(uintptr_t)(UINTPTR_MAX-10),n,&expected,&workspace,&out)==NPS_ARGUMENT);
    assert(nps_decode(0,n,&expected,&workspace,&out)==NPS_ARGUMENT);
    assert(memcmp(&out,&before,sizeof out)==0);
    printf("platform facts checks=%u atomic-output=PASS aliases=PASS\n",checks);
    return 0;
}
