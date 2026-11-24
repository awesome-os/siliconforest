/* SPDX-License-Identifier: MIT */
#ifndef SF_TB_DMA_H
#define SF_TB_DMA_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
/* SFCIODart's fixed, end-exclusive ACIO1 SID1 aperture. A mask too small to
 * cover this domain is unsupported, rather than a request to remap it. */
#define SF_TB_DMA_START UINT64_C(0x04000000)
#define SF_TB_DMA_END UINT64_C(0x14000000)
/* Installed once by the ACIO1 owner. Every backing allocation is an actual
 * NHI SID1 DART mapping, never a CPU physical address. Callbacks/ctx live until
 * reboot. Metadata lock may sleep; CM allocations run in thread context only.
 * Retire returns success only after checked DART invalidation, and retains
 * backing on failure. Allocation buffers are always zero-initialized.
 */
struct sf_tb_dma_ops {
 void *ctx,*device;
 void (*lock)(void *);void (*unlock)(void *);
 bool (*powered)(void *);
 int (*alloc)(void *,size_t,size_t,void **cpu,uint64_t *iova,void **token);
 int (*retire)(void *,void *token);
};
#ifdef __cplusplus
extern "C" {
#endif
int sf_tb_dma_install(const struct sf_tb_dma_ops *);
void sf_tb_dma_admit(bool);
/* A producer/IRQ stop failure permanently retains every mapping/backing. */
void sf_tb_dma_quarantine(void);
/* Allocation retirement inventory only; caller must separately stop/join
 * every NHI producer, IRQ and worker before treating it as part of drain. */
int sf_tb_dma_drain_status(void);
int sf_tb_dma_mask(void *device,uint64_t mask);
void *sf_tb_dma_alloc(void *device,size_t,void *dma_handle,bool may_sleep);
void sf_tb_dma_free(void *device,size_t,void *cpu,uint64_t iova);
void *sf_tb_pool_create(const char *,void *device,size_t,size_t,size_t);
void sf_tb_pool_destroy(void *);
void *sf_tb_pool_alloc(void *,void *dma_handle,bool may_sleep);
void sf_tb_pool_free(void *,void *cpu,uint64_t iova);
#ifdef __cplusplus
}
#endif
#endif
