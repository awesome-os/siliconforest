#include "route_wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error assertions required
#endif
int main(void) {
  uint8_t b[4096], old[4096]; size_t n=77;
  memset(b,0xa5,sizeof(b)); memcpy(old,b,sizeof(b));
  assert(dle_route_encode(b,159,&n,9,1,1,0x0a000203,0x0a000202,7)==-1);
  assert(n==77 && !memcmp(b,old,sizeof(b)));
  assert(dle_route_encode(b,sizeof(b),&n,9,1,1,0x0a000203,0x0a000202,7)==0);
  assert(n==160 && b[0]==160 && b[2]==5 && b[3]==1);
  assert(b[92]==16 && b[93]==2 && b[96]==10 && b[99]==3);
  assert(b[140]==20 && b[141]==18 && b[142]==7);
  struct dle_route_view v, sentinel; memset(&sentinel,0xa5,sizeof(sentinel));
  v=sentinel;
  assert(dle_route_decode(b,n,9,1,1,&v)==0);
  assert(v.destination==0x0a000203 && v.gateway==0x0a000202 &&
         v.netmask==0xffffffff && v.interface_index==7 && !v.error);
  assert(v.have_destination && v.have_gateway && v.have_netmask);
  for(size_t cut=0;cut<n;cut++) {
    v=sentinel; assert(dle_route_decode(b,cut,9,1,1,&v)==-1);
    assert(!memcmp(&v,&sentinel,sizeof(v)));
  }
  for(unsigned pos=0;pos<2;pos++) {
    v=sentinel; assert(dle_route_decode(b,n,9+pos,2-pos,1,&v)==1);
    assert(!memcmp(&v,&sentinel,sizeof(v)));
  }
  uint8_t copy[160]; memcpy(copy,b,n);
  unsigned offsets[]={0,2,3,12,92,93,94,108,109,110,124,125,140,141,142,145};
  for(size_t i=0;i<sizeof(offsets)/sizeof(*offsets);i++) {
    memcpy(b,copy,n); b[offsets[i]]=0xff; v=sentinel;
    assert(dle_route_decode(b,n,9,1,1,&v)==-1);
    assert(!memcmp(&v,&sentinel,sizeof(v)));
  }
  memcpy(b,copy,n); b[24]=3; /* ESRCH is data; caller must classify it. */
  assert(dle_route_decode(b,n,9,1,1,&v)==0 && v.error==3);
  assert(dle_route_encode(b,sizeof(b),&n,9,2,4,0x0a000203,0,0)==0 && n==108);
  assert(dle_route_decode(b,n,9,2,4,&v)==0 && v.have_destination &&
         !v.have_gateway && !v.have_netmask);
  assert(dle_route_encode(b,sizeof(b),&n,9,3,2,0x0a000203,0x0a000202,7)==0);
  assert(dle_route_decode(b,n,9,3,2,&v)==0);
  uint8_t notification[4]={4,0,5,14};v=sentinel;
  assert(dle_route_decode(notification,4,9,3,2,&v)==1);
  assert(!memcmp(&v,&sentinel,sizeof(v)));
  puts("routing wire: encode, correlation, 160 truncations, 16 corruptions, error reply, GET/DELETE PASS");
}
