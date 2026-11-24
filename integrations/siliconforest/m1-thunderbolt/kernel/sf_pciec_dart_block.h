/* SPDX-License-Identifier: GPL-2.0 */
/* Fixed T8103 PCIe-C DART layout and disable/flush ordering from pinned
 * linux-aurora apple-dart.c. Copyright The Asahi Linux Contributors;
 * based on ARM SMMU (ARM Limited) and exynos-iommu (Samsung Electronics).
 * No PARAMS reads: PCIe-C can SError on them. Caller proves sole ownership,
 * physical power/RUN/link and no bound DMA clients before every access.
 */
#ifndef SF_PCIEC_DART_BLOCK_H
#define SF_PCIEC_DART_BLOCK_H
#include <stdint.h>
#include <sys/errno.h>
struct sf_pciec_dart_block_io {
 void *ctx;
 int (*read)(void *,uint32_t,uint32_t *);
 int (*write)(void *,uint32_t,uint32_t);
 void (*delay_us)(void *,unsigned);
};
static inline int sf_pciec_dart_idle(const struct sf_pciec_dart_block_io *io)
{
 for(unsigned i=0;;++i){
  uint32_t value;int ret=io->read(io->ctx,0x20,&value);if(ret)return ret;
  if(value==UINT32_MAX)return EIO;
  if(!(value&4))return 0;
  if(i==100)return ETIMEDOUT;
  io->delay_us(io->ctx,1);
 }
}
/* Quarantine only: all64 TCRs disabled, all256 roots cleared and all64 TLBs
 * invalidated. Never enables translation, bypass, stream gates or interrupts;
 * never acknowledges a fault. It does not prove endpoint/IRQ/work drain.
 * Retain ownership/backing after ANY failed attempted write or flush.
 */
static inline int sf_pciec_dart_block(const struct sf_pciec_dart_block_io *io)
{
 if(!io || !io->ctx || !io->read || !io->write || !io->delay_us)return EINVAL;
 uint32_t value;int ret=io->read(io->ctx,0x60,&value);if(ret)return ret;
 if(value==UINT32_MAX)return EIO;
 if(value&(1u<<15))return EPERM;
 ret=sf_pciec_dart_idle(io);if(ret)return ret;
 for(unsigned sid=0;sid<64;++sid){
  uint32_t off=0x100+4*sid;
  ret=io->write(io->ctx,off,0);if(ret)return ret;
  ret=io->read(io->ctx,off,&value);if(ret)return ret;
  if(value)return EIO;
 }
 for(unsigned sid=0;sid<64;++sid)for(unsigned n=0;n<4;++n){
  uint32_t off=0x400+16*sid+4*n;
  ret=io->write(io->ctx,off,0);if(ret)return ret;
  ret=io->read(io->ctx,off,&value);if(ret)return ret;
  if(value)return EIO;
 }
 // Two selector words are essential: NHI's16-SID/0x200-root layout is wrong here.
 ret=io->write(io->ctx,0x34,UINT32_MAX);if(ret)return ret;
 ret=io->write(io->ctx,0x38,UINT32_MAX);if(ret)return ret;
 ret=io->write(io->ctx,0x20,1u<<20);if(ret)return ret;
 ret=sf_pciec_dart_idle(io);if(ret)return ret;
 for(unsigned sid=0;sid<64;++sid){
  ret=io->read(io->ctx,0x100+4*sid,&value);if(ret)return ret;
  if(value)return EIO;
 }
 return 0;
}
#endif
