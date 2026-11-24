/* Synthetic API boundary tests only; no kernel APIs or actual backing. */
#ifdef NDEBUG
#error tests require assertions
#endif
#include "probe.h"
#include "collector/collector.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static struct bcp_workspace w;
static struct bcp_record r,saved;
static struct bcp_ops ops;
static unsigned calls[8],seen[8],count,cases,get_calls;
static int fault,shape,mutate_ops;
static void mark(unsigned op)
{
    assert(count<8 && calls[op]++==0);seen[count++]=op;
    assert((bcp_word(&r,BCP_ATTEMPTS_AT)&(1u<<op)) && bcp_word(&r,BCP_STATUS_AT+4*op)==BCP_SENTINEL);
}
static int labels(void *unused,struct nps_expected *e)
{
    (void)unused;mark(0);for(unsigned i=0;i<24;i++)e->nonce[i]=(uint8_t)(i+1);
    for(unsigned i=0;i<6;i++)memset(e->sha256[i],(int)i+1,32);
    if(mutate_ops)ops.configure=NULL;return fault==1?-11:0;
}
static int configure(void *unused,const struct npp_expected *e,struct npp_workspace *scratch,struct npp_result *out)
{
    (void)unused;mark(1);assert(scratch==&w.policy && e==&w.expected && e->profile==1 && e->platform_domain==1 && e->dart_domain==2 && e->irq_domain==3);
    *out=(struct npp_result){1,1,1,4,3,6,*e};return fault==2?NPP_CONFIGURE:0;
}
static int acquire(void *unused,const struct bno_request *q,uint64_t iova,uint32_t perms,struct bno_token *t)
{
    (void)unused;mark(2);assert(q->rid==0x100 && !q->sid && q->vectors==1 && iova==0x100000 && perms==3);
    *t=(struct bno_token){&w,123,0};return fault==3?BNO_RETAINED:0;
}
static void token(const struct bno_token *t) {assert(t==&w.token && t->anchor==&w && t->generation==123 && t->slot==0);}
static int zero(void *unused,const struct bno_token *t)
{(void)unused;mark(3);token(t);return fault==4?BNO_RETAINED:0;}
static int copy(void *unused,const struct bno_token *t,uint64_t at,const void *p,size_t n)
{
    (void)unused;mark(4);token(t);assert(at==0 && p==w.expected.identity.nonce && n==16);
    assert(bcp_word(&r,BCP_COPY_BYTES_AT)==16);return fault==5?BNO_RETAINED:0;
}
static int inspect(void *unused,uint32_t slot,struct bno_snapshot *s)
{
    (void)unused;unsigned op=calls[5]?7:5;mark(op);assert(!slot && s==&w.snapshot);
    int retired=op==7;
    *s=(struct bno_snapshot){999999,888888,retired?BNO_REFS_RELEASED:BNO_INITIALIZED,0,0,1,retired?1u:0u,0,
        retired?0u:3u,3,retired?3u:0u,retired?3u:0u,0,0,0,0};
    if(shape==1 && op==5)s->effect_count=1;
    if(shape==2 && retired)s->owned_pairs=0; /* historical count must be3 */
    if(shape==3 && retired)s->identity_records=1;
    return fault==(int)op+1?-55:0;
}
static int retire(void *unused,const struct bno_token *t)
{(void)unused;mark(6);token(t);return fault==7?BNO_RETAINED:0;}
static void prepare(void)
{
    memset(&w,0,sizeof w);bcp_init(&r,1);memset(calls,0,sizeof calls);memset(seen,0,sizeof seen);count=0;fault=shape=mutate_ops=0;
    ops=(struct bcp_ops){NULL,labels,configure,acquire,zero,copy,inspect,retire};
}
static void run(void)
{bcp_run(&ops,&w,&r);cases++;}
static int get(void *opaque,uint8_t *p,size_t cap,size_t *n,int32_t *detail,int32_t *cleanup)
{
    int mode=*(int *)opaque;*detail=mode;*cleanup=mode==3?-9:0;*n=BCP_BYTES;
    get_calls++;
    assert(cap==4096);memcpy(p,r.bytes,BCP_BYTES);return mode==1?BCPC_MISSING:mode==2?BCPC_RPC:BCPC_OK;
}
int main(int argc,char **argv)
{
    if(argc==3 && !strcmp(argv[1],"emit")) {
        int mode=atoi(argv[2]);prepare();if(mode==5)bcp_init(&r,0);else {if(mode==4)fault=5;run();}
        char nonce[49];const char hex[]="0123456789abcdef";
        for(unsigned i=0;i<24;i++){unsigned v=i+1;nonce[2*i]=hex[v>>4];nonce[2*i+1]=hex[v&15];}nonce[48]=0;
        char *args[]={"test",nonce,NULL};return bcpc_run(2,args,get,&mode,stdout);
    }
    prepare();run();assert(count==8 && bcp_validate(r.bytes,BCP_BYTES,w.expected.identity.nonce)==0);saved=r;
    assert(bcp_word(&r,BCP_POST_AT+28)==3 && bcp_word(&r,BCP_POST_AT+24)==0);
    for(int f=1;f<=8;f++) {
        prepare();fault=f;run();assert(bcp_word(&r,BCP_RESULT_AT)==BCP_FAILED && bcp_word(&r,BCP_ERROR_AT));
        const int errors[8]={-11,NPP_CONFIGURE,BNO_RETAINED,BNO_RETAINED,BNO_RETAINED,-55,BNO_RETAINED,-55};
        assert(bcp_word(&r,BCP_ERROR_AT)==(uint32_t)errors[f-1]);
        assert(bcp_validate(r.bytes,BCP_BYTES,w.expected.identity.nonce)==1);
        assert(calls[6]==(f>=4));assert(calls[7]==(f>=4));
        if(f<=3)assert(count==(unsigned)f);if(f==4)assert(!calls[4]);
        for(unsigned i=0;i<8;i++)assert(calls[i]<=1);
    }
    for(int s=1;s<=3;s++){prepare();shape=s;run();assert(bcp_word(&r,BCP_RESULT_AT)==BCP_FAILED && calls[6]==1);}
    prepare();mutate_ops=1;run();assert(!ops.configure && bcp_validate(r.bytes,BCP_BYTES,w.expected.identity.nonce)==0);
    /* All single-bit changes except independent chosen label digest payloads
     * and variable legal counts must fail or stay explicitly unqualified. */
    unsigned rejected=0;
    for(unsigned at=0;at<BCP_BYTES;at++)for(unsigned bit=0;bit<8;bit++) {
        r=saved;r.bytes[at]^=(uint8_t)(1u<<bit);
        if(bcp_validate(r.bytes,BCP_BYTES,w.expected.identity.nonce)!=0)rejected++;
    }
    assert(rejected>4400);r=saved;
    bcp_put(&r,BCP_READBACK_AT,1);assert(bcp_validate(r.bytes,BCP_BYTES,w.expected.identity.nonce)==1);
    r=saved;bcp_put(&r,BCP_EFFECT_AT,1);assert(bcp_validate(r.bytes,BCP_BYTES,w.expected.identity.nonce)==1);
    for(unsigned at=608;at<BCP_BYTES;at++){r=saved;r.bytes[at]=1;assert(bcp_validate(r.bytes,BCP_BYTES,w.expected.identity.nonce)==1);}
    bcp_init(&r,0);assert(bcp_validate(r.bytes,BCP_BYTES,w.expected.identity.nonce)==77);
    for(unsigned op=0;op<8;op++){bcp_init(&r,0);bcp_put(&r,BCP_ATTEMPTS_AT,1u<<op);assert(bcp_validate(r.bytes,BCP_BYTES,w.expected.identity.nonce)==1);}
    bcp_init(&r,0);r.bytes[48]=1;assert(bcp_validate(r.bytes,BCP_BYTES,w.expected.identity.nonce)==1);
    FILE *sink=tmpfile();assert(sink);int mode=0;char *badargs[]={"test",NULL,NULL};
    const char *invalid[]={"","a","00000000000000000000000000000000000000000000000g",
        "00000000000000000000000000000000000000000000000A","--nonce"};
    for(unsigned i=0;i<sizeof invalid/sizeof invalid[0];i++) {
        badargs[1]=(char *)(uintptr_t)invalid[i];assert(bcpc_run(2,badargs,get,&mode,sink)==2);
    }
    assert(bcpc_run(1,badargs,get,&mode,sink)==2 && !get_calls);assert(!fclose(sink));
    printf("SYNTHETIC probe cases=%u onebit-refusals=%u; copy API only/no data readback\n",cases,rejected);return 0;
}
