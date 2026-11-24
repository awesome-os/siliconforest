#ifdef NDEBUG
#error DLS_TEST_REQUIRES_ASSERTIONS_NDEBUG_FORBIDDEN
#endif
#include "survey.h"
#include "observation_layout.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct property { const char *path,*key;size_t n;uint8_t bytes[2048]; };
static struct property props[32];static size_t np;
static void w32(uint8_t *p,uint32_t v) { for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i)); }
static void w64(uint8_t *p,uint64_t v) { w32(p,(uint32_t)v);w32(p+4,(uint32_t)(v>>32)); }
static struct property *add(const char *path,const char *key,const void *data,size_t n)
{
    assert(np<32&&n<=2048);struct property *p=&props[np++];
    p->path=path;p->key=key;p->n=n;memset(p->bytes,0,sizeof(p->bytes));
    if (data) memcpy(p->bytes,data,n);
    return p;
}
static struct property *find(const char *path,const char *key)
{ for (size_t i=0;i<np;i++) if (!strcmp(path,props[i].path)&&!strcmp(key,props[i].key)) return &props[i];return NULL; }
static int get(void *ctx,const char *path,const char *key,const uint8_t **p,size_t *n)
{
    (void)ctx;struct property *v=find(path,key);if (!v) return -1;
    *p=v->bytes;*n=v->n;return 0;
}
static void word(const char *path,const char *key,uint32_t v)
{ w32(add(path,key,NULL,4)->bytes,v); }
static void reg(uint8_t *p,unsigned idx,uint64_t base,uint64_t size)
{ w64(p+16*idx,base);w64(p+16*idx+8,size); }
static struct dls_board fixture(void)
{
    np=0;struct dls_board b;struct property *p;
    add("/","target-type","J293",5);
    add("/","compatible","J293AP\0MacBookPro17,1\0AppleARM",sizeof("J293AP\0MacBookPro17,1\0AppleARM"));
    add("/arm-io","compatible","arm-io,t8103",13);
    add("/arm-io/apcie","compatible","apcie,t8103",12);
    add("/arm-io/dart-apcie0","compatible","dart,t8020",11);
    add("/arm-io/pmgr","compatible","pmgr1,t8103",12);
    add("/arm-io/aic","compatible","aic,1",6);
    p=add("/arm-io","ranges",NULL,72);
    w64(p->bytes,0);w64(p->bytes+8,UINT64_C(0x200000000));w64(p->bytes+16,UINT64_C(0x100000000));
    w64(p->bytes+24,UINT64_C(0x600000000));w64(p->bytes+32,UINT64_C(0x600000000));w64(p->bytes+40,UINT64_C(0x100000000));
    memcpy(p->bytes+48,p->bytes+24,24); // redundant identical answer accepted
    p=add("/arm-io/apcie","reg",NULL,21*16);
    reg(p->bytes,1,UINT64_C(0x680000000),0x4000);reg(p->bytes,2,UINT64_C(0x680080000),0x90000);
    reg(p->bytes,6,UINT64_C(0x681000000),0x8000);
    p=add("/arm-io/dart-apcie0","reg",NULL,16);reg(p->bytes,0,UINT64_C(0x681008000),0x4000);
    p=add("/arm-io/pmgr","reg",NULL,54*16);reg(p->bytes,0,0x3b700000,0x8c000);
    p=add("/arm-io/pmgr","ps-regs",NULL,13*12);w32(p->bytes+5*12+4,0x300);w32(p->bytes+11*12+4,0x100);
    p=add("/arm-io/pmgr","devices",NULL,4*48);
    const unsigned ids[]={32,77,97,29},parents[]={0,32,77,0},offsets[]={23,9,29,20},ps[]={11,5,5,11};
    const char *names[]={"IMX","APCIE","APCIE_GP","PCIE_REF"};
    for (unsigned i=0;i<4;i++) {
        uint8_t *d=p->bytes+i*48;d[0]=1;d[4]=(uint8_t)parents[i];d[10]=(uint8_t)offsets[i];
        d[11]=(uint8_t)ps[i];d[26]=(uint8_t)ids[i];memcpy(d+32,names[i],strlen(names[i])+1);
    }
    p=add("/arm-io/apcie","clock-gates",NULL,12);w32(p->bytes,77);w32(p->bytes+4,97);w32(p->bytes+8,29);
    add("/arm-io/apcie","power-gates",p->bytes,12);
    p=add("/arm-io/apcie","bus-range",NULL,8);w32(p->bytes+4,8);
    word("/arm-io/apcie","#ports",3);
    word("/arm-io/dart-apcie0","page-size",0x4000);word("/arm-io/dart-apcie0","vm-base",0x100000);
    word("/arm-io/dart-apcie0","vm-size",0xbfe00000);word("/arm-io/dart-apcie0","dart-options",0x17);
    word("/arm-io/dart-apcie0","manual-availability",1);word("/arm-io/dart-apcie0","interrupts",696);
    word("/arm-io/dart-apcie0","interrupt-parent",112);word("/arm-io/aic","AAPL,phandle",112);
    assert(dls_validate_board(get,NULL,&b)==DLS_SAMPLED);return b;
}
static void rejected(void)
{
    struct dls_board before,out;memset(&before,0xa5,sizeof(before));out=before;
    assert(dls_validate_board(get,NULL,&out)==DLS_BOARD);assert(!memcmp(&before,&out,sizeof(out)));
}
static void boards(void)
{
    struct dls_board b=fixture();assert(b.base[DLS_DART]==UINT64_C(0x681008000));
    assert(dls_validate_board(NULL,NULL,&b)==DLS_ARGUMENT);
    assert(dls_validate_board(get,NULL,NULL)==DLS_ARGUMENT);
    for (size_t i=0;i<np;i++) { size_t n=props[i].n;props[i].n=0;rejected();props[i].n=n; }
    find("/","target-type")->bytes[0]='X';rejected();fixture();
    find("/arm-io/dart-apcie0","compatible")->bytes[6]='9';rejected();fixture();
    w64(find("/arm-io","ranges")->bytes+56,UINT64_C(0x600004000));rejected();fixture();
    w64(find("/arm-io","ranges")->bytes+40,UINT64_MAX);rejected();fixture();
    w64(find("/arm-io/apcie","reg")->bytes+16,UINT64_C(0x880000000));rejected();fixture();
    w32(find("/arm-io/dart-apcie0","interrupt-parent")->bytes,111);rejected();fixture();
    struct property *p=find("/arm-io/pmgr","devices");
    p->bytes[48+4]=33;rejected();fixture();p=find("/arm-io/pmgr","devices");
    p->bytes[6]=1;rejected();fixture();p=find("/arm-io/pmgr","devices");
    p->bytes[0]|=0x10;rejected();fixture();p=find("/arm-io/pmgr","devices");
    p->bytes[11]=13;rejected();fixture();p=find("/arm-io/pmgr","devices");
    p->bytes[10]++;rejected();fixture();p=find("/arm-io/pmgr","devices");
    memcpy(p->bytes+4*48,p->bytes,48);p->n+=48;rejected();fixture();
    w32(find("/arm-io/pmgr","ps-regs")->bytes+11*12,54);rejected();fixture();
    w32(find("/arm-io/pmgr","ps-regs")->bytes+11*12+4,UINT32_MAX);rejected();fixture();
    w64(find("/arm-io/pmgr","reg")->bytes,UINT64_MAX-8);rejected();fixture();
}

struct fake {
    uint32_t power[4],clocks[5],dart[0x300/4];
    unsigned reads,dart_reads,stage,fail_at,loss_after;
};
static const unsigned regs[9]={DLS_PMGR,DLS_PMGR,DLS_PMGR,DLS_PMGR,DLS_PHY,DLS_RC,DLS_PORT,DLS_PORT,DLS_PORT};
static const uint32_t offs[9]={0x1b8,0x348,0x3e8,0x1a0,0,0x24,0x800,0x814,0x804};
static struct fake ready(void)
{
    struct fake f={0};for (unsigned i=0;i<4;i++) f.power[i]=0xff;
    for (unsigned i=0;i<5;i++) f.clocks[i]=i?1:15;
    f.dart[0]=14u<<24;f.dart[1]=1;f.dart[0xfc/4]=0xffff;return f;
}
static int read32(void *ctx,unsigned r,uint32_t off,uint32_t *out)
{
    struct fake *f=ctx;assert(dls_read_allowed(r,off));f->reads++;
    if (f->reads==f->fail_at) return -1;
    if (r==DLS_DART) {
        // Every DART read must have exactly four power + five clock reads first.
        assert(f->stage==9);assert(off<sizeof(f->dart));f->stage=0;f->dart_reads++;
        *out=f->dart[off/4];return 0;
    }
    assert(f->stage<9&&r==regs[f->stage]&&off==offs[f->stage]);
    *out=f->stage<4?f->power[f->stage]:f->clocks[f->stage-4];
    if (f->loss_after&&f->dart_reads>=f->loss_after&&r==DLS_PMGR) *out=0;
    f->stage++;return 0;
}
static int run(struct fake *f,struct dls_snapshot *s)
{
    struct dls_board b=fixture();struct dls_io io={f,read32};return dls_sample(&b,&io,s);
}
static void normal(void)
{
    struct fake f=ready(),before=f;struct dls_snapshot s;assert(run(&f,&s)==DLS_SAMPLED);
    assert(s.result==DLS_SAMPLED&&s.sample_count==88&&s.reads_attempted==889);
    assert(f.dart_reads==88&&f.stage==9&&s.failure_region==DLS_REGIONS);
    assert(!memcmp(f.power,before.power,sizeof(f.power))&&!memcmp(f.clocks,before.clocks,sizeof(f.clocks)));
    assert(!memcmp(f.dart,before.dart,sizeof(f.dart))); // fake register bank stayed byte-identical
    for (unsigned i=0;i<88;i++) {
        assert(dls_read_allowed(DLS_DART,s.samples[i].offset));
        for (unsigned j=0;j<i;j++) assert(s.samples[j].offset!=s.samples[i].offset);
    }
}
static void prerequisites(void)
{
    struct dls_snapshot s;
    const uint32_t bad[]={0,0xf,0xf0,0x4f,0x800000ff,0x100000ff,0x10ff,0x8ff,0x4ff};
    for (unsigned d=0;d<4;d++) for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        struct fake f=ready();f.power[d]=bad[i];assert(run(&f,&s)==DLS_NOT_READY);assert(f.dart_reads==0);
    }
    for (unsigned c=0;c<5;c++) {
        struct fake f=ready();f.clocks[c]=0;assert(run(&f,&s)==DLS_NOT_READY);assert(f.dart_reads==0);
    }
    for (unsigned bit=0;bit<4;bit++) {
        struct fake f=ready();f.clocks[0]&=~(1u<<bit);assert(run(&f,&s)==DLS_NOT_READY);assert(f.dart_reads==0);
    }
    struct fake f=ready();f.clocks[0]|=0x80;assert(run(&f,&s)==DLS_NOT_READY);assert(f.dart_reads==0);
    f=ready();f.power[0]|=0x300;assert(run(&f,&s)==DLS_SAMPLED); // historical gated status is not current power
    for (unsigned n=1;n<=88;n++) {
        f=ready();f.loss_after=n;assert(run(&f,&s)==DLS_NOT_READY);assert(f.dart_reads==n&&s.sample_count==n);
    }
}
static void refusal(void)
{
    struct dls_snapshot s;struct fake f=ready();f.dart[0]=12u<<24;
    assert(run(&f,&s)==DLS_UNSUPPORTED&&s.sample_count==1);
    f=ready();f.dart[0x60/4]=1u<<15;assert(run(&f,&s)==DLS_UNSUPPORTED&&s.sample_count==3);
    f=ready();f.dart[0xfc/4]=0x10000;assert(run(&f,&s)==DLS_UNSUPPORTED&&s.sample_count==4);
    struct dls_board b=fixture();struct dls_io io={&f,read32};
    unsigned reads=f.reads;b.base[DLS_DART]+=0x4000;
    assert(dls_sample(&b,&io,&s)==DLS_BOARD&&f.reads==reads);
    assert(dls_sample(NULL,&io,&s)==DLS_ARGUMENT);assert(dls_sample(&b,NULL,&s)==DLS_ARGUMENT);
    assert(dls_sample(&b,&io,NULL)==DLS_ARGUMENT);io.read32=NULL;
    assert(dls_sample(&b,&io,&s)==DLS_ARGUMENT);
}
static void faults(void)
{
    for (unsigned n=1;n<=889;n++) {
        struct fake f=ready();f.fail_at=n;struct dls_snapshot s;
        assert(run(&f,&s)==DLS_IO&&s.result==DLS_IO&&s.reads_attempted==n&&f.reads==n);
        assert(s.sample_count==f.dart_reads&&s.failure_region<DLS_REGIONS);
    }
}
static void allowlist(void)
{
    for (unsigned r=0;r<=DLS_REGIONS;r++) for (unsigned o=0;o<0x10000;o++) {
        int expected=0;
        for (unsigned i=0;i<9;i++) if (r==regs[i]&&o==offs[i]) expected=1;
        if (r==DLS_DART&&!(o&3)) expected=o==0||o==4||o==0x60||o==0xfc||
            (o>=0x80&&o<=0x8c)||(o>=0x100&&o<=0x13c)||(o>=0x200&&o<=0x2fc);
        assert(dls_read_allowed(r,o)==expected);
    }
    assert(!dls_read_allowed(DLS_DART,UINT32_MAX));assert(!dls_read_allowed(UINT32_MAX,0));
}
int main(void)
{
    boards();normal();prerequisites();refusal();faults();allowlist();
    puts("HOST_ONLY 6 groups; 889 read-fault positions; 88 power-loss boundaries; zero fake register writes");return 0;
}
