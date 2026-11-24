/* SPDX-License-Identifier: GPL-2.0 */
#include "sf_pciec_requesters.h"
#include <assert.h>
#include <stdio.h>
struct model { uint32_t words[64];unsigned reads,writes,fail_read;bool fail_write,bad_readback; };
static unsigned index_of(uint32_t off){assert(off>=0x828 && off<=0x924 && !(off&3));return (off-0x828)/4;}
static int read_reg(void *p,uint32_t off,uint32_t *v)
{struct model *m=p;++m->reads;if(m->fail_read==m->reads)return EIO;*v=m->words[index_of(off)]^(m->bad_readback && m->writes?1u:0);return 0;}
static int write_reg(void *p,uint32_t off,uint32_t v)
{struct model *m=p;++m->writes;m->words[index_of(off)]=v;return m->fail_write?EIO:0;}
static int reserve(struct model *m,struct sf_pciec_requesters *t,unsigned b,unsigned d,unsigned f,uint32_t *sid)
{struct sf_pciec_requester_io io={m,read_reg,write_reg};return sf_pciec_requester_reserve(t,&io,b,d,f,sid);}
int main(void)
{
 struct model m={0};struct sf_pciec_requesters t={0};uint32_t sid=99;
 assert(reserve(&m,&t,0,0,0,&sid)==EINVAL);
 assert(reserve(&m,&t,128,0,0,&sid)==EINVAL);
 assert(reserve(&m,&t,1,32,0,&sid)==EINVAL);
 assert(reserve(&m,&t,1,0,8,&sid)==EINVAL);
 assert(!m.reads && !m.writes && sid==99);
 /* Dynamic bus numbers, shared endpoint SID0, and bridge device SIDs. */
 assert(!reserve(&m,&t,3,0,0,&sid) && sid==0 && m.words[0]==0x80000300);
 assert(!reserve(&m,&t,7,0,0,&sid) && sid==0 && m.words[1]==0x80000700);
 assert(!reserve(&m,&t,2,1,0,&sid) && sid==8 && m.words[2]==0x80080208);
 assert(!reserve(&m,&t,2,4,0,&sid) && sid==32 && m.words[3]==0x80200220);
 assert(!reserve(&m,&t,127,31,7,&sid) && sid==63 && m.words[4]==0x803f7fff);
 unsigned writes=m.writes;
 assert(!reserve(&m,&t,2,1,0,&sid) && sid==8 && m.writes==writes);
 for(unsigned i=5;i<64;++i)assert(!reserve(&m,&t,10+i,0,0,&sid));
 sid=99;assert(reserve(&m,&t,100,0,0,&sid)==ENOSPC && sid==99 && !t.poisoned);
 for(unsigned fail=1;fail<=2;++fail){
  m=(struct model){.fail_read=fail};t=(struct sf_pciec_requesters){0};sid=99;
  assert(reserve(&m,&t,3,0,0,&sid)==EIO && t.poisoned && sid==99);
  writes=m.writes;assert(reserve(&m,&t,4,0,0,&sid)==EIO && m.writes==writes);
 }
 m=(struct model){.fail_write=true};t=(struct sf_pciec_requesters){0};sid=99;
 assert(reserve(&m,&t,3,0,0,&sid)==EIO && t.poisoned && t.values[0] && sid==99);
 m=(struct model){.bad_readback=true};t=(struct sf_pciec_requesters){0};
 assert(reserve(&m,&t,3,0,0,&sid)==EIO && t.poisoned && sid==99);
 m=(struct model){.words={0x80000100}};t=(struct sf_pciec_requesters){0};
 assert(reserve(&m,&t,3,0,0,&sid)==EBUSY && t.poisoned && !m.writes);
 m=(struct model){0};t=(struct sf_pciec_requesters){0};
 assert(!reserve(&m,&t,3,0,0,&sid));m.words[0]^=1;sid=99;
 assert(reserve(&m,&t,3,0,0,&sid)==EIO && t.poisoned && sid==99);
 puts("T8103 PCIe-C actual RID2SID helper: bounds/sharedSID/capacity/duplicate/fault retention PASS");
}
