#include "inventory.h"
#include "route_wire.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#error assertions required
#endif
static unsigned checks;
#define CHECK(e) do{checks++;assert(e);}while(0)
int main(void){
    struct dlf_route_set *a=calloc(1,sizeof(*a)),*b=calloc(1,sizeof(*b)),*w=calloc(1,sizeof(*w));
    CHECK(a&&b&&w);uint8_t msg[192],copy[192];size_t n=0;struct dlf_owner owner={4,"utun1"};
    CHECK(!dlf_encode(msg,sizeof(msg),&n,42,1,&owner));msg[3]=4;msg[144]=1;
    CHECK(!dlf_inventory(NULL,0,a,w));CHECK(!a->count);CHECK(!dlf_inventory_has_default(a));
    CHECK(!dlf_inventory(msg,n,a,w));CHECK(a->count==1);
    CHECK(dlf_inventory_has_default(a)==1);
    a->key[0].flags|=0x01000000;CHECK(dlf_inventory_has_default(a)==1); /* foreign scoped default */
    a->key[0].flags&=~0x01000000u;a->key[0].body[4]=10;CHECK(!dlf_inventory_has_default(a));a->key[0].body[4]=0;
    a->key[0].body[36]=128;CHECK(!dlf_inventory_has_default(a));a->key[0].body[36]=0;
    memcpy(copy,msg,n);copy[60]=99;copy[16]=9;copy[20]=9;
    CHECK(!dlf_inventory(copy,n,b,w));CHECK(dlf_inventory_equal(a,b)); /* metrics/PID not config */
    copy[4]++;CHECK(!dlf_inventory(copy,n,b,w));CHECK(!dlf_inventory_equal(a,b));
    memcpy(copy,msg,n);copy[8]^=1;CHECK(!dlf_inventory(copy,n,b,w));CHECK(!dlf_inventory_equal(a,b));
    memcpy(copy,msg,n);copy[112]^=1;CHECK(!dlf_inventory(copy,n,b,w));CHECK(!dlf_inventory_equal(a,b));
    for(size_t i=1;i<n;i++){
        memset(b,0xa5,sizeof(*b));struct dlf_route_set *saved=malloc(sizeof(*saved));CHECK(saved);memcpy(saved,b,sizeof(*b));
        CHECK(dlf_inventory(msg,i,b,w));CHECK(!memcmp(saved,b,sizeof(*b)));free(saved);
    }
    uint8_t *many=malloc(129*n);CHECK(many);
    for(unsigned i=0;i<129;i++){memcpy(many+i*n,msg,n);many[i*n+4]=(uint8_t)(i+1);}
    CHECK(!dlf_inventory(many,128*n,b,w));CHECK(b->count==128);CHECK(dlf_inventory(many,129*n,b,w));
    for(unsigned i=0;i<64;i++)for(size_t j=0;j<n;j++){
        uint8_t byte=many[i*n+j];many[i*n+j]=many[(127-i)*n+j];many[(127-i)*n+j]=byte;
    }
    CHECK(!dlf_inventory(many,128*n,a,w));CHECK(dlf_inventory_equal(a,b)); /* failed parse preserved b */
    memcpy(many,msg,n);memcpy(many+n,msg,n);CHECK(dlf_inventory(many,2*n,b,w));
    memcpy(copy,msg,n);copy[140+13]=1;CHECK(dlf_inventory(copy,n,b,w)); /* link padding */
    memcpy(copy,msg,n);copy[12]|=0x80;CHECK(dlf_inventory(copy,n,b,w)); /* missing sockaddr */
    free(many);free(a);free(b);free(w);printf("IPv4 route-config inventory: %u checks PASS\n",checks);return 0;
}
