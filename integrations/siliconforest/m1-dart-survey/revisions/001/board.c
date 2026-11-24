/* Original bounded ADT checks; no MMIO. Pinned facts: PROVENANCE.json. */
#include "survey.h"
#include <string.h>

static uint32_t u32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint64_t u64(const uint8_t *p) { return u32(p)|(uint64_t)u32(p+4)<<32; }
static unsigned u16(const uint8_t *p) { return p[0]|(unsigned)p[1]<<8; }
struct view { dls_property_fn get; void *ctx; };
static int prop(struct view *v,const char *path,const char *key,const uint8_t **p,size_t *n)
{ *p=0;*n=0;return !v->get(v->ctx,path,key,p,n)&&*p&&*n<=32768; }
static int bytes(struct view *v,const char *path,const char *key,const char *s,size_t len)
{ const uint8_t *p;size_t n;return prop(v,path,key,&p,&n)&&n==len&&!memcmp(p,s,len); }
static int words(struct view *v,const char *path,const char *key,const uint32_t *values,size_t count)
{
    const uint8_t *p;size_t n;
    if (!prop(v,path,key,&p,&n)||n!=4*count) return 0;
    for (size_t i=0;i<count;i++) if (u32(p+4*i)!=values[i]) return 0;
    return 1;
}
static int word(struct view *v,const char *path,const char *key,uint32_t value)
{ return words(v,path,key,&value,1); }

/* Resolve exactly one physical answer, accepting redundant identity ranges
 * only if every matching tuple gives that same answer. Never add 0x200000000
 * to already-physical APCIE entries. All additions checked before use.
 */
static int translate(struct view *v,uint64_t addr,uint64_t size,uint64_t expected)
{
    const uint8_t *p;size_t n;int found=0;
    if (!size||addr>UINT64_MAX-size||!prop(v,"/arm-io","ranges",&p,&n)||!n||n%24||n/24>32) return 0;
    for (size_t i=0;i<n;i+=24) {
        uint64_t child=u64(p+i),parent=u64(p+i+8),len=u64(p+i+16);
        if (!len||child>UINT64_MAX-len||parent>UINT64_MAX-len) return 0;
        if (addr>=child&&size<=len&&addr-child<=len-size) {
            if (parent+(addr-child)!=expected) return 0;
            found=1;
        }
    }
    return found;
}
static int reg_entry(struct view *v,const char *path,size_t count,size_t index,
                     uint64_t address,uint64_t size)
{
    const uint8_t *p;size_t n;
    return prop(v,path,"reg",&p,&n)&&n==count*16&&index<count&&
        u64(p+16*index+8)==size&&translate(v,u64(p+16*index),size,address);
}

struct domain { unsigned id,parent;const char *name;uint64_t address; };
static const struct domain domains[4]={
    {32,0,"IMX",UINT64_C(0x23b7001b8)},
    {77,32,"APCIE",UINT64_C(0x23b700348)},
    {97,77,"APCIE_GP",UINT64_C(0x23b7003e8)},
    {29,0,"PCIE_REF",UINT64_C(0x23b7001a0)}
};
static int power_table(struct view *v)
{
    const uint8_t *d,*ps,*r;size_t dn,pn,rn;
    if (!prop(v,"/arm-io/pmgr","devices",&d,&dn)||!dn||dn%48||dn/48>512||
        !prop(v,"/arm-io/pmgr","ps-regs",&ps,&pn)||!pn||pn%12||pn/12>64||
        !prop(v,"/arm-io/pmgr","reg",&r,&rn)||!rn||rn%16||rn/16>64) return 0;
    for (unsigned k=0;k<4;k++) {
        const uint8_t *entry=0;
        for (size_t j=0;j<dn;j+=48) if (u16(d+j+26)==domains[k].id) {
            if (entry) return 0;
            entry=d+j;
        }
        if (!entry||(entry[0]&0x10)||u16(entry+4)!=domains[k].parent||u16(entry+6)||
            memcmp(entry+32,domains[k].name,strlen(domains[k].name)+1)) return 0;
        size_t idx=entry[11];
        if (idx>=pn/12) return 0;
        uint32_t ri=u32(ps+idx*12);
        uint64_t offset=u32(ps+idx*12+4)+(uint64_t)entry[10]*8;
        if (ri>=rn/16) return 0;
        uint64_t base=u64(r+ri*16),len=u64(r+ri*16+8);
        if (len<4||offset>len-4||base>UINT64_MAX-len||
            !translate(v,base+offset,4,domains[k].address)) return 0;
    }
    return 1;
}

int dls_validate_board(dls_property_fn get,void *ctx,struct dls_board *out)
{
    struct view v={get,ctx};const uint8_t *aic;size_t n;
    const uint32_t gates[]={77,97,29},bus[]={0,8};
    const struct dls_board board={
        {UINT64_C(0x23b700000),UINT64_C(0x680000000),UINT64_C(0x680080000),UINT64_C(0x681000000),UINT64_C(0x681008000)},
        {0x4000,0x4000,0x4000,0x4000,0x4000}};
    if (!get||!out) return DLS_ARGUMENT;
    if (!bytes(&v,"/","target-type","J293",5)||
        !bytes(&v,"/","compatible","J293AP\0MacBookPro17,1\0AppleARM",sizeof("J293AP\0MacBookPro17,1\0AppleARM"))||
        !bytes(&v,"/arm-io","compatible","arm-io,t8103",13)||
        !bytes(&v,"/arm-io/apcie","compatible","apcie,t8103",12)||
        !bytes(&v,"/arm-io/dart-apcie0","compatible","dart,t8020",11)||
        !bytes(&v,"/arm-io/pmgr","compatible","pmgr1,t8103",12)||
        !bytes(&v,"/arm-io/aic","compatible","aic,1",6)) return DLS_BOARD;
    if (!reg_entry(&v,"/arm-io/apcie",21,1,board.base[DLS_RC],0x4000)||
        !reg_entry(&v,"/arm-io/apcie",21,2,board.base[DLS_PHY],0x90000)||
        !reg_entry(&v,"/arm-io/apcie",21,6,board.base[DLS_PORT],0x8000)||
        !reg_entry(&v,"/arm-io/dart-apcie0",1,0,board.base[DLS_DART],0x4000)||
        !reg_entry(&v,"/arm-io/pmgr",54,0,board.base[DLS_PMGR],0x8c000)||
        !power_table(&v)) return DLS_BOARD;
    if (!words(&v,"/arm-io/apcie","clock-gates",gates,3)||
        !words(&v,"/arm-io/apcie","power-gates",gates,3)||
        !words(&v,"/arm-io/apcie","bus-range",bus,2)||!word(&v,"/arm-io/apcie","#ports",3)||
        !word(&v,"/arm-io/dart-apcie0","page-size",0x4000)||
        !word(&v,"/arm-io/dart-apcie0","vm-base",0x100000)||
        !word(&v,"/arm-io/dart-apcie0","vm-size",0xbfe00000)||
        !word(&v,"/arm-io/dart-apcie0","dart-options",0x17)||
        !word(&v,"/arm-io/dart-apcie0","manual-availability",1)||
        !word(&v,"/arm-io/dart-apcie0","interrupts",696)||
        !prop(&v,"/arm-io/aic","AAPL,phandle",&aic,&n)||n!=4||!u32(aic)||
        !word(&v,"/arm-io/dart-apcie0","interrupt-parent",u32(aic))) return DLS_BOARD;
    *out=board;
    return DLS_SAMPLED; /* Board validation only, no read or readiness claim. */
}
