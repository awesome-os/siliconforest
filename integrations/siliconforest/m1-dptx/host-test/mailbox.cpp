/* SPDX-License-Identifier: MIT */
#include "sf_dcp_mailbox_plan.h"
#include <cassert>
#include <cstring>
#include <cstdio>
int main()
{
 for(unsigned ext=0;ext<2;++ext){
  const uint64_t base=ext?0x271c00000ULL:0x231c00000ULL;
  const uint32_t send=ext?466:427,recv=ext?469:430;
  sf_dcp_mailbox_plan p={};assert(!sf_dcp_mailbox_plan_t8103(base,0x4000,&p));
  assert(!strcmp(p.path,ext?"/arm-io/dcpext":"/arm-io/dcp"));
  assert(!sf_dcp_mailbox_check_t8103(&p,base,0xc000,base+0x8000,0x4000,send,recv));
  assert(!sf_dcp_mailbox_check_t8103(&p,base,0x10000,base+0x8000,0x4000,send,recv));
  for(unsigned bit=0;bit<64;++bit){
   assert(sf_dcp_mailbox_check_t8103(&p,base^(1ULL<<bit),0xc000,base+0x8000,0x4000,send,recv)==-ENODEV);
   assert(sf_dcp_mailbox_check_t8103(&p,base,0xc000,(base+0x8000)^(1ULL<<bit),0x4000,send,recv)==-ENODEV);
  }
  assert(sf_dcp_mailbox_check_t8103(&p,base,0xbfff,base+0x8000,0x4000,send,recv)==-ENODEV);
  assert(sf_dcp_mailbox_check_t8103(&p,base,UINT64_MAX,base+0x8000,0x4000,send,recv)==-ENODEV);
  assert(sf_dcp_mailbox_check_t8103(&p,base,0xc000,base+0x8000,0x4001,send,recv)==-ENODEV);
  assert(sf_dcp_mailbox_check_t8103(&p,base,0xc000,base+0x8000,0x4000,recv,send)==-ENODEV);
  auto old=p;
  assert(sf_dcp_mailbox_plan_t8103(base,0x3fff,&p)==-EINVAL && p.coproc==old.coproc);
  assert(sf_dcp_mailbox_plan_t8103(base,UINT64_MAX,&p)==-EINVAL && p.coproc==old.coproc);
  assert(sf_dcp_mailbox_plan_t8103(base+4,0x4000,&p)==-ENODEV && p.coproc==old.coproc);
 }
 assert(sf_dcp_mailbox_plan_t8103(0,0x4000,nullptr)==-EINVAL);
 assert(sf_dcp_mailbox_check_t8103(nullptr,0,0,0,0,0,0)==-EINVAL);
 puts("T8103 DCP mailbox host model: actual-resource association, internal/external separation, wrong base/span/IRQ and unchanged-on-error checks passed; no target binding/start");
}
