/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * sf_dart.h - one stream of an Apple T8020 DART that firmware provisioned (the display
 * DARTs of T8103): the stream's TCR and TTBR are never written, its live root is reused,
 * tables and entries are only added. Plain C types; implemented by src/sf_dart.cpp (IOKit).
 *
 * Streams used by the display on J293:
 *   /arm-io/dart-dcp    stream 0   the dcp device (RTKit buffers, AFK rings, shared memory)
 *   /arm-io/dart-disp0  stream 0   the display-subsystem device (framebuffers; iBoot's live
 *                                  boot framebuffer is mapped here)
 *   /arm-io/dart-disp0  stream 4   the dcp's piodma child
 */
#ifndef SF_DART_H
#define SF_DART_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct sf_dart_stream;

struct sf_dart_info {
	char		path[64];	/* device-tree path of the DART */
	uint64_t	regs_phys;
	uint32_t	sid;
	uint32_t	params1, config, tcr, ttbr[4], enabled_streams, error;
	int		locked;		/* CONFIG bit 15 */
	uint64_t	l1_phys;	/* root named by TTBR 0 */
	uint32_t	l1_valid;	/* valid first-level entries */
	uint64_t	window_lo, window_hi;	/* iova range allocations come from */
};

/*
 * Opens (once; later calls return the same object) the stream `sid` of the DART at
 * device-tree path `path`, or of the DART whose reg 0 is `regs_phys` when path is NULL.
 * Read-only: maps the registers and the live root, checks that the stream is enabled and
 * translating with a valid TTBR 0, and sets up the allocation window. NULL on failure
 * (the reason is logged).
 */
struct sf_dart_stream *sf_dart_open(const char *path, uint64_t regs_phys, uint32_t sid);
void sf_dart_get_info(struct sf_dart_stream *s, struct sf_dart_info *info);

/* Read-only: physical address for an iova of this stream, 0 if not mapped. */
uint64_t sf_dart_translate(struct sf_dart_stream *s, uint64_t iova);
/* Read-only: both entries of the walk, for logs. Returns the physical address or 0. */
uint64_t sf_dart_walk(struct sf_dart_stream *s, uint64_t iova, uint64_t *l1e, uint64_t *pte);
/* Read-only: lowest iova at which [phys, phys + size) is mapped contiguously; 0 on success. */
int sf_dart_find(struct sf_dart_stream *s, uint64_t phys, uint64_t size, uint64_t *iova);

/*
 * Maps [phys, phys + size) at `iova` (page aligned), then invalidates the stream's TLB.
 * prot: SF_DART_PROT_READ | SF_DART_PROT_WRITE (sf_dart_table.h). 0 or a negative errno;
 * -EEXIST when part of the range is already mapped.
 */
int sf_dart_map(struct sf_dart_stream *s, uint64_t iova, uint64_t phys, uint64_t size, unsigned int prot);
/*
 * The same without the TLB invalidate, for a caller that maps many ranges: every run of
 * sf_dart_map_deferred() calls must end with one sf_dart_flush(), also when a call failed.
 */
int sf_dart_map_deferred(struct sf_dart_stream *s, uint64_t iova, uint64_t phys, uint64_t size, unsigned int prot);
int sf_dart_flush(struct sf_dart_stream *s);
/* Clears the range and invalidates. Zero on an invalidate failure. Callers
 * freeing backing MUST use unmap_checked and retain backing on failure. */
uint64_t sf_dart_unmap(struct sf_dart_stream *s, uint64_t iova, uint64_t size);
/* Output unchanged on failure; poisoned streams cannot allocate/map/reclaim. */
int sf_dart_unmap_checked(struct sf_dart_stream *,uint64_t iova,uint64_t size,uint64_t *unmapped);
/* Conservative across this DCP copy: PIODMA may share any buffer. */
int sf_dart_poisoned(struct sf_dart_stream *);

/*
 * Reserves `size` bytes of iova in the stream's window (first fit, page granular) without
 * mapping anything. Returns 0 and *iova, or -ENOSPC. sf_dart_iova_free gives it back.
 */
int sf_dart_iova_alloc(struct sf_dart_stream *s, uint64_t size, uint64_t *iova);
void sf_dart_iova_free(struct sf_dart_stream *s, uint64_t iova, uint64_t size);
/* Atomic reservation/reclamation of the same IOVA in two native streams;
 * second may be NULL. Both allocations stay reserved on any poisoned DART. */
int sf_dart_iova_alloc_pair(struct sf_dart_stream *,struct sf_dart_stream *,uint64_t size,uint64_t *iova);
void sf_dart_iova_free_pair(struct sf_dart_stream *,struct sf_dart_stream *,uint64_t iova,uint64_t size);

/* The DART's error register (read, never cleared); a flagged fault is logged with `when`. */
uint32_t sf_dart_check(struct sf_dart_stream *s, const char *when);

/* Counters: first-level entries written, leaves written, leaves cleared, TLB invalidates. */
void sf_dart_stats(struct sf_dart_stream *s, uint32_t *l1_writes, uint32_t *leaf_writes,
		   uint32_t *leaf_clears, uint32_t *invalidates);

/* Line prefix for this module's kernel log lines (default "SFDcpDart: "); string must stay valid. */
void sf_dart_set_log_prefix(const char *prefix);

#ifdef __cplusplus
}
#endif

#endif
