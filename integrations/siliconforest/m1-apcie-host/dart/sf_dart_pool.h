/* SPDX-License-Identifier: GPL-2.0 */
/* Static DMA bounce pool behind dart-apcie0 (T8020 DART), stream 1 (J293 WLAN/BT). */
#ifndef _SF_DART_POOL_H
#define _SF_DART_POOL_H
#include <sys/types.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int   sf_dart_pool_init(void);                                       /* 0 on success; idempotent */
void *sf_dart_pool_alloc(size_t size, size_t align, uint64_t *iova); /* zeroed; NULL on failure */
void  sf_dart_pool_free(void *kva, size_t size);
/* Logs and clears a pending DART fault. Returns the raw error register (0 = none). */
uint32_t sf_dart_pool_check(const char *why);
#ifdef __cplusplus
}
#endif
#endif
