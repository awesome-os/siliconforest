#include "default_route.h"
#include "route_wire.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#ifdef NDEBUG
#error assertions required
#endif
static unsigned checks;
#define CHECK(e) do {checks++;assert(e);} while(0)
struct fixture {
    struct dlf_session session;
    unsigned get,opened,added,closed,inventory,route,foreign;
    unsigned fail_get,fail_open,fail_add,fail_close,fail_inventory,wrong_readback,wrong_owner;
};
static const struct dlf_owner identity={4,"utun1"};
static struct dlf_route valid(void){
    struct dlf_route r={0};r.flags=DLF_REQUIRED_FLAGS;r.gateway=DLF_PEER;r.ifa=DLF_LOCAL;
    r.index=r.ifp_index=identity.index;r.have_destination=r.have_gateway=r.have_netmask=r.have_ifa=r.have_ifp=1;
    memcpy(r.ifp_name,identity.name,sizeof(r.ifp_name));return r;
}
static int query(void *c,struct dlf_route *out){
    struct fixture *f=c;f->get++;
    if(f->get==f->fail_get)return -1;
    struct dlf_route r={0};r.have_destination=1;r.error=3;
    if(f->route||f->foreign){r=valid();if(f->foreign)r.index=r.ifp_index=99;}
    if(f->wrong_readback&&f->route)r.gateway++;
    *out=r;return 0;
}
static int owner_open(void *c,struct dlf_owner *o){
    struct fixture *f=c;f->opened++;*o=identity;if(f->wrong_owner)o->index=0;return f->fail_open?-1:0;
}
static int add(void *c,const struct dlf_owner *o){
    struct fixture *f=c;f->added++;CHECK(o->index==identity.index);f->route=1;
    /* Deliberately installed even when reply is lost. */
    return f->fail_add?-1:0;
}
static int owner_close(void *c){struct fixture *f=c;f->closed++;f->route=0;return f->fail_close?-1:0;}
static int inventory(void *c){struct fixture *f=c;f->inventory++;return f->fail_inventory?-1:0;}
static const struct dlf_ops ops={query,owner_open,add,owner_close,inventory};
static void init(struct fixture *f){memset(f,0,sizeof(*f));CHECK(!dlf_init(&f->session,&ops,f,1));}
static void lifetime(void){
    struct fixture f;init(&f);CHECK(!dlf_open(&f.session));CHECK(f.session.stage==DLF_ACTIVE&&f.route);
    struct dlf_session saved=f.session;CHECK(dlf_init(&f.session,&ops,&f,1));CHECK(!memcmp(&saved,&f.session,sizeof(saved))&&f.route);
    CHECK(!dlf_close(&f.session));CHECK(!f.route&&f.closed==1&&f.session.cleanup_complete);
    CHECK(!dlf_close(&f.session));CHECK(f.closed==1&&f.added==1);CHECK(dlf_open(&f.session));
    init(&f);f.foreign=1;CHECK(dlf_open(&f.session));CHECK(!f.opened&&!f.added&&!f.closed&&f.foreign);
    init(&f);f.fail_add=1;CHECK(dlf_open(&f.session));CHECK(f.added==1&&f.closed==1&&!f.route);
    CHECK(f.session.first_error==DLF_ADD&&f.session.cleanup_complete);CHECK(dlf_close(&f.session));CHECK(f.closed==1);
    init(&f);f.fail_open=1;CHECK(dlf_open(&f.session));CHECK(f.closed==1&&!f.added);
    init(&f);f.wrong_owner=1;CHECK(dlf_open(&f.session));CHECK(f.closed==1&&!f.added);
    init(&f);f.wrong_readback=1;CHECK(dlf_open(&f.session));CHECK(f.closed==1&&!f.route);
    for(unsigned i=1;i<=4;i++){
        init(&f);f.fail_get=i;int rc=dlf_open(&f.session);
        if(i<=3)CHECK(rc);else CHECK(!rc);
        (void)dlf_close(&f.session);CHECK(f.closed==(i==1?0:1));
        if(i==4)CHECK(!f.session.cleanup_complete);
    }
    init(&f);CHECK(!dlf_open(&f.session));f.fail_close=1;CHECK(dlf_close(&f.session));
    CHECK(!f.session.cleanup_complete&&f.closed==1);CHECK(dlf_close(&f.session));CHECK(f.closed==1);
    init(&f);CHECK(!dlf_open(&f.session));f.fail_inventory=1;CHECK(dlf_close(&f.session));CHECK(!f.session.cleanup_complete);
    init(&f);CHECK(!dlf_open(&f.session));f.foreign=1;CHECK(dlf_close(&f.session));
    CHECK(f.foreign&&!f.route&&!f.session.cleanup_complete); /* Never delete another interface's route. */
    CHECK(dlf_init(&f.session,&ops,&f,0));CHECK(dlf_open(&f.session));CHECK(!f.added||f.closed==1);
}
static void predicates(void){
    struct dlf_route good=valid(),r;
    CHECK(dlf_route_matches(&good,&identity));
    for(unsigned bit=0;bit<32;bit++){r=good;r.flags^=1u<<bit;CHECK(!dlf_route_matches(&r,&identity));}
    for(unsigned i=0;i<10;i++){
        r=good;switch(i){case 0:r.error=1;break;case 1:r.destination=1;break;case 2:r.gateway++;break;
        case 3:r.netmask=1;break;case 4:r.ifa++;break;case 5:r.index++;break;case 6:r.ifp_index++;break;
        case 7:r.ifp_name[0]='x';break;case 8:r.have_ifa=0;break;case 9:r.have_netmask=0;break;}
        CHECK(!dlf_route_matches(&r,&identity));
    }
}
static void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(i*8));}
static void wire(void){
    uint8_t b[192],copy[192];size_t n=0;struct dlf_route out;
    CHECK(!dlf_encode(b,sizeof(b),&n,42,1,&identity));CHECK(n==172);
    CHECK(b[3]==1&&b[12]==0x37&&b[124]==16&&b[125]==2); /* full /0 before IFP */
    CHECK(b[140]==13&&b[141]==18&&b[142]==4&&b[145]==5&&!memcmp(b+148,"utun1",5));
    CHECK(!dlf_decode(b,n,42,1,&out));CHECK(out.netmask==0&&out.have_ifa&&out.ifa==DLF_LOCAL);
    CHECK(!dlf_encode(b,sizeof(b),&n,42,4,NULL));CHECK(n==148&&b[12]==0x35);
    memcpy(copy,b,n);size_t untouched=123;CHECK(dlf_encode(b,sizeof(b),&untouched,42,2,&identity));CHECK(untouched==123&&!memcmp(copy,b,n));
    CHECK(!dlf_encode(b,sizeof(b),&n,42,1,&identity));b[3]=4;b[144]=1;put32(b+8,DLF_REQUIRED_FLAGS);
    CHECK(dlf_decode(b,n,42,4,&out)); /* compact ACK is not actual GET link metadata */
    memmove(b+160,b+156,16);memset(b+153,0,7);b[140]=20;n+=4;b[0]=(uint8_t)n;
    CHECK(!dlf_decode(b,n,42,4,&out));CHECK(dlf_route_matches(&out,&identity));
    /* Kernel reply has compressed mask (four zero bytes), with IFP/IFA following. */
    memmove(b+128,b+140,n-140);memset(b+124,0,4);n-=12;b[0]=(uint8_t)n;b[1]=0;
    CHECK(!dlf_decode(b,n,42,4,&out));CHECK(dlf_route_matches(&out,&identity));
    struct dlf_route sentinel;memset(&sentinel,0xa5,sizeof(sentinel));
    for(size_t k=0;k<n;k++){out=sentinel;CHECK(dlf_decode(b,k,42,4,&out));CHECK(!memcmp(&out,&sentinel,sizeof(out)));}
    memcpy(copy,b,n);
    for(unsigned i=0;i<4;i++){memcpy(b,copy,n);b[124+i]=1;out=sentinel;CHECK(dlf_decode(b,n,42,4,&out));CHECK(!memcmp(&out,&sentinel,sizeof(out)));}
    memcpy(b,copy,n);b[3]=2;CHECK(dlf_decode(b,n,42,4,&out));
    memcpy(b,copy,n);b[20]=2;CHECK(dlf_decode(b,n,42,4,&out)==1);
    memcpy(b,copy,n);b[16]=43;CHECK(dlf_decode(b,n,42,4,&out)==1);
    memcpy(b,copy,n);b[4]=99;CHECK(!dlf_decode(b,n,42,4,&out));CHECK(!dlf_route_matches(&out,&identity));
    for(size_t i=0;i<n;i++)for(unsigned bit=0;bit<8;bit++){
        memcpy(b,copy,n);b[i]^=(uint8_t)(1u<<bit);out=sentinel;
        int rc=dlf_decode(b,n,42,4,&out);
        if(rc)CHECK(!memcmp(&out,&sentinel,sizeof(out)));
        checks++;
    }
}
int main(void){lifetime();predicates();wire();printf("owned default lifetime/wire: %u checks PASS\n",checks);return 0;}
