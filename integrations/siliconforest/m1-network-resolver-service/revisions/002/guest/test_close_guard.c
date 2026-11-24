#ifdef NDEBUG
#error assertions must be enabled
#endif
#include "close_guard.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { assert(x);checks++; } while(0)
int main(void) {
 struct dlr_drain d;memset(&d,0,sizeof(d));CHECK(!dlr_drain_begin(&d,1000,40000));CHECK(d.deadline==41000);CHECK(dlr_drain_begin(&d,1001,40000)<0);
 CHECK(dlr_drain_poll(&d,1001,0,0,0,0,0,0)==0);CHECK(dlr_drain_poll(&d,1002,1,1,0,0,0,1)==0);CHECK(dlr_drain_poll(&d,1003,1,0,1,0,0,1)==0);CHECK(dlr_drain_poll(&d,1004,1,0,0,0,0,1)==1);CHECK(d.completed==1004&&d.error==0);CHECK(dlr_drain_poll(&d,1005,1,0,0,0,0,1)<0);
 for(unsigned peer=0;peer<2;peer++)for(unsigned out=0;out<18;out++)for(unsigned in=0;in<18;in++)for(unsigned line=0;line<4;line++)for(unsigned bits=0;bits<9;bits++)for(unsigned end=0;end<2;end++) {
  memset(&d,0,sizeof(d));CHECK(!dlr_drain_begin(&d,1000,40000));int rc=dlr_drain_poll(&d,1001,(int)peer,out,in,line,bits,(int)end);
  if(out>16||in>16||line>2||bits>7)CHECK(rc<0&&d.error==DLR_DRAIN_ARGUMENT);
  else if(peer&&!out&&!in)CHECK(rc==((line||bits||!end)?-1:1));else CHECK(rc==0);
 }
 /* Same incomplete physical stream: old15 expires at16s, new40 still waits;
  * the actual final codecCLOSE+empty queues at32s completes only the new one. */
 struct dlr_drain old,new;memset(&old,0,sizeof(old));memset(&new,0,sizeof(new));CHECK(!dlr_drain_begin(&old,1000,15000));CHECK(!dlr_drain_begin(&new,1000,40000));CHECK(dlr_drain_poll(&old,16000,0,0,0,1,3,0)<0&&old.error==DLR_DRAIN_TIMEOUT);CHECK(dlr_drain_poll(&new,16000,0,0,0,1,3,0)==0);CHECK(dlr_drain_poll(&new,32000,1,1,0,0,0,1)==0);CHECK(dlr_drain_poll(&new,32001,1,0,0,0,0,1)==1);
 memset(&d,0,sizeof(d));CHECK(dlr_drain_begin(&d,0,40000)<0&&!d.state);CHECK(dlr_drain_begin(&d,UINT64_MAX-39999,40000)<0&&!d.state);CHECK(dlr_drain_begin(&d,1000,40001)<0&&!d.state);CHECK(!dlr_drain_begin(&d,1000,40000));CHECK(dlr_drain_poll(&d,999,0,0,0,0,0,0)<0&&d.error==DLR_DRAIN_CLOCK);CHECK(dlr_drain_poll(&d,1001,1,0,0,0,0,1)<0);
 memset(&d,0,sizeof(d));CHECK(!dlr_drain_begin(&d,1000,40000));CHECK(dlr_drain_poll(&d,41000,1,0,0,0,0,1)<0&&d.error==DLR_DRAIN_TIMEOUT);
 printf("close_guard: %u boundary/state assertions passed\n",checks);return 0;
}
