#include "close_guard.h"
int dlr_drain_begin(struct dlr_drain *d,uint64_t now,unsigned budget) {
 if(!d||d->state||!now||(budget!=15000&&budget!=40000)||now>UINT64_MAX-budget)return -1;
 d->started=d->last=now;d->deadline=now+budget;d->state=1;d->error=DLR_DRAIN_OK;return 0;
}
static int failed(struct dlr_drain *d,enum dlr_drain_error error) {d->state=3;if(!d->error)d->error=error;return -1;}
int dlr_drain_poll(struct dlr_drain *d,uint64_t now,int peer,unsigned output,unsigned input,unsigned line,unsigned bits,int end) {
 if(!d||d->state!=1)return -1;
 if((peer!=0&&peer!=1)||(end!=0&&end!=1)||output>16||input>16||line>2||bits>7)return failed(d,DLR_DRAIN_ARGUMENT);
 if(!now||now<d->last)return failed(d,DLR_DRAIN_CLOCK);
 d->last=now;
 if(now>=d->deadline)return failed(d,DLR_DRAIN_TIMEOUT);
 if(peer&&!output&&!input) {
  if(line||bits||!end)return failed(d,DLR_DRAIN_STREAM);
  d->completed=now;d->state=2;return 1;
 }
 return 0;
}
