/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Per-buffer DMA mapping behind dart-apcie0 (T8020 DART), stream 1 (J293 WLAN/BT).
 *
 * Every address the device is given is an IOVA from one window; nothing is mapped that a
 * caller did not ask for. Mapping is page granular (16 KiB): a buffer that does not start
 * on a page boundary maps its whole page(s), and its device address is iova + offset.
 *
 * TLB discipline, after Linux apple-dart.c (which invalidates after a map as well as after
 * an unmap): a new mapping may only be used by the device after sf_dart_sync() returned 0.
 * An unmap clears the entries at once but the IOVA is only handed out again after the next
 * invalidate, so unmaps cost no invalidate of their own. Define SF_DART_STRICT to invalidate
 * inside every map and unmap instead.
 *
 * All functions return 0 or an errno value unless stated. They take one mutex and may
 * allocate: call them from thread context (a work loop is fine), never from a primary
 * interrupt.
 */
#ifndef _SF_DART_MAP_H
#define _SF_DART_MAP_H
#include <sys/types.h>
#include <stdint.h>

#define SF_DART_MAP_PAGE_SHIFT  14
#define SF_DART_MAP_PAGE_SIZE   (1UL << SF_DART_MAP_PAGE_SHIFT)
#define SF_DART_MAP_PAGE_MASK   (SF_DART_MAP_PAGE_SIZE - 1)

#ifdef __cplusplus
extern "C" {
#endif

/* Programs stream 1 with an empty table and turns translation on. Idempotent. */
int sf_dart_init(void);

/*
 * Maps npages physical pages, which need not be adjacent, at consecutive IOVAs.
 * Every pa[] must be non-zero, 16 KiB aligned and below 2^36. *iova is page aligned.
 */
int sf_dart_map_pages(const uint64_t *pa, unsigned npages, uint64_t *iova);

/*
 * Maps one physically contiguous run. pa may point into a page: the whole pages that
 * [pa, pa + size) touches are mapped and *dva is the device address of pa itself.
 */
int sf_dart_map(uint64_t pa, size_t size, uint64_t *dva);

/*
 * After the DART may have lost its registers (system sleep, the PCIe port powered down and
 * up again): if the stream no longer translates through our table, program it again as
 * sf_dart_init() did. The tables and every mapping in them are kept. Call it only while
 * the DART is powered, that is with the port up. 0 when the stream translates afterwards
 * or the mapper was never initialised. UNTESTED on hardware.
 */
int sf_dart_resume(void);

/* Unmaps the pages that [dva, dva + size) touches; takes what sf_dart_map() returned. */
int sf_dart_unmap(uint64_t dva, size_t size);

/* Invalidates the stream's TLB if a mapping was made since the last invalidate. */
int sf_dart_sync(void);

/*
 * Wired, zeroed, page aligned kernel memory, mapped and already synced. The memory is not
 * physically contiguous but its IOVA range is. NULL on failure.
 */
void *sf_dart_alloc(size_t size, uint64_t *iova);
void  sf_dart_free(void *kva);

/* Physical address behind a wired kernel virtual address, 0 when there is none. */
uint64_t sf_dart_kvtophys(const void *kva);

/* Logs and clears a pending DART fault. Returns the raw error register (0 = none). */
uint32_t sf_dart_check(const char *why);

/* One line: pages mapped now and at the peak, tables, operations, failures. */
void sf_dart_report(const char *who);

#ifdef __cplusplus
}
#endif
#endif
