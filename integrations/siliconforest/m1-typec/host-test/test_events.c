/* SPDX-License-Identifier: GPL-2.0 */
#include "sf_cd321x_events.h"
#include <assert.h>
#include <stdio.h>
struct model { unsigned reads,writes;int fail_read,fail_write;uint8_t pending[8],cleared[8]; };
static int read_reg(void *p,uint8_t reg,void *dst,size_t len)
{
 struct model *m=p;assert(reg==0x14 && len==8);++m->reads;
 if(m->fail_read)return EIO;
 memcpy(dst,m->pending,8);return 0;
}
static int write_reg(void *p,uint8_t reg,const void *src,size_t len)
{
 struct model *m=p;assert(m->reads==1 && reg==0x18 && len==8);++m->writes;
 memcpy(m->cleared,src,8);return m->fail_write?EIO:0;
}
int main(void)
{
 struct model m={0};struct sf_cd321x_events_result out;
 assert(sf_cd321x_events_ack(NULL,read_reg,write_reg,&out)==EINVAL);
 assert(sf_cd321x_events_ack(&m,NULL,write_reg,&out)==EINVAL);
 assert(sf_cd321x_events_ack(&m,read_reg,NULL,&out)==EINVAL);
 assert(sf_cd321x_events_ack(&m,read_reg,write_reg,NULL)==EINVAL);
 assert(!m.reads && !m.writes);
 assert(!sf_cd321x_events_ack(&m,read_reg,write_reg,&out));
 assert(out.read_valid && !out.acknowledge_attempted && !out.acknowledged && !m.writes);
 m=(struct model){.fail_read=1};
 assert(sf_cd321x_events_ack(&m,read_reg,write_reg,&out)==EIO);
 assert(!out.read_valid && !out.acknowledge_attempted && !out.acknowledged && !m.writes);
 /* Every single bit, including high-byte reset/vendor events, is retained
  * exactly; no guessed mask or future/unobserved event is cleared. */
 for(unsigned bit=0;bit<64;++bit){
  m=(struct model){0};m.pending[bit/8]=(uint8_t)(1u<<(bit%8));
  assert(!sf_cd321x_events_ack(&m,read_reg,write_reg,&out));
  assert(out.read_valid && out.acknowledge_attempted && out.acknowledged && m.writes==1);
  assert(!memcmp(m.pending,m.cleared,8) && !memcmp(out.pending,m.pending,8));
 }
 m=(struct model){.fail_write=1,.pending={2,4,8,16,32,64,128,255}};
 assert(sf_cd321x_events_ack(&m,read_reg,write_reg,&out)==EIO);
 assert(out.read_valid && out.acknowledge_attempted && !out.acknowledged && m.writes==1);
 assert(!memcmp(out.pending,m.pending,8));
 puts("CD321x EVENT1/CLEAR1: zero, all64bits, failed-read and ambiguous-write PASS");
}
