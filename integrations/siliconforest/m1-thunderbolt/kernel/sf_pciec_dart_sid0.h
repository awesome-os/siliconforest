/* SPDX-License-Identifier: GPL-2.0 */
/* T8103 PCIe-C SID0 transaction, from pinned Linux apple-dart.c.
 * Copyright The Asahi Linux Contributors; based on ARM SMMU (ARM Limited)
 * and exynos-iommu (Samsung Electronics). Source/host component only.
 * The native owner must serialize all access, prove the real activated
 * tunnel/power/RUN/link and keep every endpoint BME blocked during prepare.
 * Every read/write callback must revalidate that ownership. No PARAMS,
 * remap, bypass, interrupt enable or fault acknowledgement is performed.
 */
#ifndef SF_PCIEC_DART_SID0_H
#define SF_PCIEC_DART_SID0_H
#include "sf_pciec_dart_block.h"
#include <stdbool.h>
struct sf_pciec_dart_sid0_io {
 struct sf_pciec_dart_block_io regs;
 // Publish the actual retained, initialized table pages before TTBR/flush.
 int (*publish_tables)(void *);
};
static inline int sf_pciec_dart_sid0_check(const struct sf_pciec_dart_sid0_io *io)
{
 if(!io || !io->regs.ctx || !io->regs.read || !io->regs.write ||
    !io->regs.delay_us || !io->publish_tables)return EINVAL;
 uint32_t value;int ret=io->regs.read(io->regs.ctx,0x60,&value);
 if(ret)return ret;
 if(value==UINT32_MAX)return EIO;
 if(value&(1u<<15))return EPERM;
 ret=io->regs.read(io->regs.ctx,0x40,&value);if(ret)return ret;
 // Preserve the first hardware fault. A later caller cannot clear it here.
 if(value&(1u<<31))return EIO;
 return sf_pciec_dart_idle(&io->regs);
}
static inline int sf_pciec_dart_sid0_flush(const struct sf_pciec_dart_sid0_io *io)
{
 int ret=io->publish_tables(io->regs.ctx);if(ret)return ret;
 ret=sf_pciec_dart_idle(&io->regs);if(ret)return ret;
 ret=io->regs.write(io->regs.ctx,0x34,1);if(ret)return ret;
 // This is a64-SID DART, even though this domain admits SID0 only.
 ret=io->regs.write(io->regs.ctx,0x38,0);if(ret)return ret;
 ret=io->regs.write(io->regs.ctx,0x20,1u<<20);if(ret)return ret;
 return sf_pciec_dart_idle(&io->regs);
}
static inline int sf_pciec_dart_sid0_write(const struct sf_pciec_dart_sid0_io *io,
 uint32_t off,uint32_t value)
{
 int ret=io->regs.write(io->regs.ctx,off,value);if(ret)return ret;
 uint32_t actual;ret=io->regs.read(io->regs.ctx,off,&actual);
 return ret?ret:actual==value?0:EIO;
}
static inline int sf_pciec_dart_sid0_bank(const struct sf_pciec_dart_sid0_io *io,
 uint32_t root,uint32_t control)
{
 uint32_t value;int ret;
 for(unsigned sid=0;sid<64;++sid){
  ret=io->regs.read(io->regs.ctx,0x100+4*sid,&value);if(ret)return ret;
  if(value!=(sid?0:control))return EBUSY;
 }
 for(unsigned n=0;n<256;++n){
  ret=io->regs.read(io->regs.ctx,0x400+4*n,&value);if(ret)return ret;
  if(value!=(n?0:root))return EBUSY;
 }
 return 0;
}
/* Accept only the fully blocked64-stream bank, not a claimed software flag.
 * root is an owned16KiB table below the fixed36-bit output limit. A caller
 * retains every table/backing and quarantines on any failed attempted write.
 * Success is translation setup, never endpoint/interrupt/DMA completion.
 */
static inline int sf_pciec_dart_sid0_prepare(const struct sf_pciec_dart_sid0_io *io,
 uint64_t root,bool *touched)
{
 if(!touched)return EINVAL;
 *touched=false;
 if(!root || (root&0x3fff) || root>=(1ULL<<36))return EINVAL;
 int ret=sf_pciec_dart_sid0_check(io);if(ret)return ret;
 ret=sf_pciec_dart_sid0_bank(io,0,0);if(ret)return ret;
 ret=io->publish_tables(io->regs.ctx);if(ret)return ret;
 uint32_t enable;ret=io->regs.read(io->regs.ctx,0xfc,&enable);if(ret)return ret;
 *touched=true; // Includes an ambiguous failed first write.
 ret=sf_pciec_dart_sid0_write(io,0x400,(1u<<31)|(uint32_t)(root>>12));if(ret)return ret;
 ret=sf_pciec_dart_sid0_flush(io);if(ret)return ret;
 // Enable SID0's global gate, preserving other gates. TCRs still isolate
 // every other stream. Do not copy the NHI layout or write0x100 as an
 // upper global-enable word: it is also this bank's SID0 TCR.
 ret=sf_pciec_dart_sid0_write(io,0xfc,enable|1u);if(ret)return ret;
 ret=sf_pciec_dart_sid0_write(io,0x100,1u<<7);if(ret)return ret;
 ret=sf_pciec_dart_sid0_flush(io);if(ret)return ret;
 return sf_pciec_dart_sid0_check(io);
}
/* Caller has stopped the real endpoint, masked/joined IRQs and drained work.
 * A completed TLB flush is only the final memory-retirement prerequisite.
 * On any error retain ownership/backing; do not recycle IOVAs or table pages.
 */
static inline int sf_pciec_dart_sid0_disable(const struct sf_pciec_dart_sid0_io *io,
 uint64_t root)
{
 if(!root || (root&0x3fff) || root>=(1ULL<<36))return EINVAL;
 int ret=sf_pciec_dart_sid0_check(io);if(ret)return ret;
 ret=sf_pciec_dart_sid0_bank(io,(1u<<31)|(uint32_t)(root>>12),1u<<7);
 if(ret)return ret;
 ret=sf_pciec_dart_sid0_write(io,0x100,0);if(ret)return ret;
 ret=sf_pciec_dart_sid0_flush(io);if(ret)return ret;
 for(unsigned n=0;n<4;++n){
  ret=sf_pciec_dart_sid0_write(io,0x400+4*n,0);if(ret)return ret;
 }
 return sf_pciec_dart_sid0_flush(io);
}
#endif
