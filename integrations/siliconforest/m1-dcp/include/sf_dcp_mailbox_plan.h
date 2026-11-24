/* SPDX-License-Identifier: MIT */
/* T8103 DCP/mailbox resource association from linux-aurora 6a08a08e
 * arch/arm64/boot/dts/apple/t8103.dtsi. No mapping or hardware operations. */
#ifndef SF_DCP_MAILBOX_PLAN_H
#define SF_DCP_MAILBOX_PLAN_H
#include <stdint.h>
#include <errno.h>
struct sf_dcp_mailbox_plan {
 const char *path;
 uint64_t coproc,mailbox;
 uint32_t send_empty,recv_not_empty;
};
/* Linux's coproc resource excludes the distinct mailbox resource. */
static inline int sf_dcp_mailbox_plan_t8103(uint64_t coproc,uint64_t bytes,
                                           struct sf_dcp_mailbox_plan *out)
{
 if(!out || bytes<0x4000 || bytes>UINT64_MAX-coproc)return -EINVAL;
 struct sf_dcp_mailbox_plan p;
 if(coproc==0x231c00000ULL){
  p.path="/arm-io/dcp";p.send_empty=427;p.recv_not_empty=430;
 }else if(coproc==0x271c00000ULL){
  p.path="/arm-io/dcpext";p.send_empty=466;p.recv_not_empty=469;
 }else return -ENODEV;
 p.coproc=coproc;p.mailbox=coproc+0x8000;*out=p;return 0;
}
static inline int sf_dcp_mailbox_check_t8103(const struct sf_dcp_mailbox_plan *p,
 uint64_t asc,uint64_t asc_bytes,uint64_t mailbox,uint64_t mailbox_bytes,
 uint32_t send_empty,uint32_t recv_not_empty)
{
 if(!p)return -EINVAL;
 return asc==p->coproc && asc_bytes>=0xc000 && asc_bytes<=UINT64_MAX-asc &&
  mailbox==p->mailbox && mailbox_bytes==0x4000 &&
  send_empty==p->send_empty && recv_not_empty==p->recv_not_empty?0:-ENODEV;
}
#endif
