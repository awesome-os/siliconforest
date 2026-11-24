/* Original bounded ADT checks; no MMIO. Pinned facts: PROVENANCE.json. */
#include "preflight.h"
#include <string.h>

static uint32_t u32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint64_t u64(const uint8_t *p) { return u32(p)|(uint64_t)u32(p+4)<<32; }
static unsigned u16(const uint8_t *p) { return p[0]|(unsigned)p[1]<<8; }
struct view { gpf_property_fn get; void *ctx; };
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


static int domains(struct view *v)
{
    static const struct { unsigned id,parent,flags;const char *name; } want[]={
        {99,0,0xa2,"GFX"},{314,99,0x10,"GFX-SGX"},
        {315,0,0x10,"GFX-BUSY"},{316,99,0x10,"GFX-ASC"}};
    const uint8_t *d,*ps,*r;size_t dn,pn,rn;
    if (!prop(v,"/arm-io/pmgr","devices",&d,&dn)||!dn||dn%48||dn/48>512||
        !prop(v,"/arm-io/pmgr","ps-regs",&ps,&pn)||!pn||pn%12||pn/12>64||
        !prop(v,"/arm-io/pmgr","reg",&r,&rn)||!rn||rn%16||rn/16>64) return 0;
    for (unsigned k=0;k<4;k++) {
        const uint8_t *entry=0;
        for (size_t j=0;j<dn;j+=48) if (u16(d+j+26)==want[k].id) {
            if (entry) return 0;
            entry=d+j;
        }
        if (!entry || entry[0]!=want[k].flags || u16(entry+4)!=want[k].parent ||
            u16(entry+6) || memcmp(entry+32,want[k].name,strlen(want[k].name)+1)) return 0;
        /* Virtual GFX-SGX/BUSY/ASC entries have NO physical PS-register.
         * Never interpret their placeholder offset/index bytes as MMIO. */
        if (entry[0]&0x10) continue;
        size_t idx=entry[11];if (idx>=pn/12) return 0;
        uint32_t ri=u32(ps+idx*12);
        uint64_t offset=u32(ps+idx*12+4)+(uint64_t)entry[10]*8;
        if (ri>=rn/16) return 0;
        uint64_t b=u64(r+ri*16),len=u64(r+ri*16+8);
        if (len<4||offset>len-4||b>UINT64_MAX-len||
            !translate(v,b+offset,4,UINT64_C(0x23b7003f8))) return 0;
    }
    return 1;
}
int gpf_validate_board(gpf_property_fn get,void *ctx,struct gpf_board *out)
{
    struct view v={get,ctx};
    const uint32_t sgx[]={314,315},asc[]={316};
    const struct gpf_board board={UINT64_C(0x23b700000),0x4000,0x3f8};
    if (!get||!out) return GPF_BOARD_REFUSED;
    if (!bytes(&v,"/","target-type","J293",5)||
        !bytes(&v,"/","compatible","J293AP\0MacBookPro17,1\0AppleARM",sizeof("J293AP\0MacBookPro17,1\0AppleARM"))||
        !bytes(&v,"/arm-io","compatible","arm-io,t8103",13)||
        !bytes(&v,"/arm-io/sgx","compatible","gpu,t8103",10)||
        !bytes(&v,"/arm-io/gfx-asc","compatible","iop,ascwrap-v4",sizeof("iop,ascwrap-v4"))||
        !bytes(&v,"/arm-io/pmgr","compatible","pmgr1,t8103",12)||
        !reg_entry(&v,"/arm-io/sgx",2,0,UINT64_C(0x204000000),0x4000000)||
        !reg_entry(&v,"/arm-io/sgx",2,1,UINT64_C(0x204d00000),0x20000)||
        !reg_entry(&v,"/arm-io/gfx-asc",2,0,UINT64_C(0x206400000),0x6c000)||
        !reg_entry(&v,"/arm-io/gfx-asc",2,1,UINT64_C(0x206050000),8)||
        !reg_entry(&v,"/arm-io/pmgr",54,0,board.pmgr_base,0x8c000)||
        !words(&v,"/arm-io/sgx","clock-gates",sgx,2)||
        !words(&v,"/arm-io/gfx-asc","clock-gates",asc,1)||
        !words(&v,"/arm-io/gfx-asc","power-gates",asc,1)||!domains(&v)) return GPF_BOARD_REFUSED;
    *out=board;return GPF_OBSERVED;
}
