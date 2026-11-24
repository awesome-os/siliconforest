/* SPDX-License-Identifier: GPL-2.0 */
/* T8103 MSI setup from linux-aurora 6a08a08e pcie-apple.c.
 * Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 * Copyright (C) 2021 Google LLC
 * Copyright (C) 2021 Corellium LLC
 * Copyright (C) 2021 Mark Kettenis <kettenis@openbsd.org>
 */
#ifndef SF_PCIEC_MSI_SETUP_H
#define SF_PCIEC_MSI_SETUP_H
#include <stdint.h>
#include <errno.h>
struct sf_pciec_msi_setup_io {
 void *ctx;
 int (*read)(void *,uint32_t,uint32_t *);
 int (*write)(void *,uint32_t,uint32_t);
};
struct sf_pciec_msi_setup_report {
 uint32_t before[3],after[3],before_count,after_count;
};
/* A real backend must check retained ownership, physical power/RUN/link
 * before EVERY operation. Configuration is not IRQ delivery or DMA proof. */
static inline int sf_pciec_msi_setup(const struct sf_pciec_msi_setup_io *io,
 struct sf_pciec_msi_setup_report *report)
{
 if(!io || !io->read || !io->write || !report)return EINVAL;
 const uint32_t offsets[3]={0x124,0x128,0x168};
 const uint32_t expected[3]={0x51,0,0xfffff000};
 report->before_count=report->after_count=0;
 for(unsigned n=0;n<3;++n){
  int ret=io->read(io->ctx,offsets[n],&report->before[n]);if(ret)return ret;
  ++report->before_count;
  if(report->before[n]==UINT32_MAX)return ENODEV;
 }
 const struct {uint32_t off,value;} setup[]={
  {0x104,UINT32_MAX},{0x100,UINT32_MAX},{0x210,UINT32_MAX},
  {0x168,0xfffff000},{0x128,0},{0x124,0x51}
 };
 for(unsigned n=0;n<6;++n){
  int ret=io->write(io->ctx,setup[n].off,setup[n].value);if(ret)return ret;
 }
 for(unsigned n=0;n<3;++n){
  int ret=io->read(io->ctx,offsets[n],&report->after[n]);if(ret)return ret;
  ++report->after_count;
  if(report->after[n]!=expected[n])return EIO;
 }
 return 0;
}
#endif
