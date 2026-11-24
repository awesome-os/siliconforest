/* SPDX-License-Identifier: GPL-2.0 */
/* T8103 RID2SID layout from pinned linux-aurora pcie-apple.c; iommu-map-mask
 * 0x3f from its t8103 PCIe-C device tree. Copyright Alyssa Rosenzweig,
 * Google LLC, Corellium LLC and Mark Kettenis. Caller serializes this table,
 * verifies actual tunnel/RUN/link/power, and owns all64 cold-cleared slots.
 * Reservation is not DART/MSI/BME admission. No removal/reuse until real
 * device/IRQ/work/TLB drain exists; an uncertain write poisons the table.
 */
#ifndef SF_PCIEC_REQUESTERS_H
#define SF_PCIEC_REQUESTERS_H
#include <stdint.h>
#include <stdbool.h>
#include <sys/errno.h>
struct sf_pciec_requesters { uint32_t values[64];bool poisoned; };
struct sf_pciec_requester_io {
 void *ctx;
 int (*read)(void *,uint32_t,uint32_t *);
 int (*write)(void *,uint32_t,uint32_t);
};
static inline int sf_pciec_requester_reserve(struct sf_pciec_requesters *table,
 const struct sf_pciec_requester_io *io,unsigned bus,unsigned dev,unsigned fn,
 uint32_t *sid_out)
{
 if(!table || !io || !io->ctx || !io->read || !io->write || !sid_out ||
    !bus || bus>127 || dev>31 || fn>7)return EINVAL;
 if(table->poisoned)return EIO;
 uint32_t rid=(bus<<8)|(dev<<3)|fn,sid=rid&0x3f;
 uint32_t word=0x80000000u|(sid<<16)|rid;
 unsigned free_slot=64;
 for(unsigned i=0;i<64;++i){
  if(!table->values[i]){if(free_slot==64)free_slot=i;continue;}
  if((table->values[i]&0xffff)!=rid)continue;
  uint32_t value;int ret=io->read(io->ctx,0x828+4*i,&value);
  if(ret || value!=word){table->poisoned=true;return ret?ret:EIO;}
  *sid_out=sid;return 0;
 }
 if(free_slot==64)return ENOSPC;
 uint32_t off=0x828+4*free_slot,value;
 int ret=io->read(io->ctx,off,&value);
 if(ret || value){table->poisoned=true;return ret?ret:EBUSY;}
 table->values[free_slot]=word; // reserve before any ambiguous write
 ret=io->write(io->ctx,off,word);
 if(!ret)ret=io->read(io->ctx,off,&value);
 if(ret || value!=word){table->poisoned=true;return ret?ret:EIO;}
 *sid_out=sid;return 0;
}
#endif
