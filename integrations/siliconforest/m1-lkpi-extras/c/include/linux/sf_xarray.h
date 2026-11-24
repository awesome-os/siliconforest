/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Linux XArray entry points that FreeBSD LinuxKPI's xarray (linux/xarray.h, a wrapper
 * around its radix tree) lacks or implements with other semantics. They operate on the
 * same `struct xarray`, so an array may be used through both sets of functions.
 *
 * Why not LinuxKPI's own functions (linux_xarray.c):
 *   - index is uint32_t there; Linux (and rust/kernel/xarray.rs) pass unsigned long
 *   - __xa_insert() answers -EEXIST for an occupied index, Linux -EBUSY
 *   - __xa_alloc() answers -ENOMEM for a full range, Linux -EBUSY; it also refuses
 *     limit.min == limit.max by assertion
 *   - __xa_next() (xa_for_each) stops at the first reserved entry instead of skipping it
 *   - there is no xa_find(), xa_find_after(), __xa_cmpxchg(), xa_trylock(), XA_ZERO_ENTRY
 *
 * Semantics are those of Linux 7.2 lib/xarray.c for the normal API, with these limits:
 *   - indices above UINT32_MAX are refused (-EINVAL; lookups find nothing). LinuxKPI's
 *     own API cannot reach them, and the radix tree's iterator wraps at the top level.
 *   - no marks: the only filter xa_find()/xa_find_after() accept is XA_PRESENT
 *   - no multi-index entries, no RCU readers: every function runs under xa_lock, which
 *     is a recursive sleepable mutex in this layer (the callers that already hold it, as
 *     rust/kernel/xarray.rs does around xa_load() and xa_find(), simply recurse)
 *   - the pointer value 0x1 cannot be stored (LinuxKPI keeps it for reserved entries), and
 *     a reservation is made by storing NULL, not by passing SF_XA_ZERO_ENTRY to
 *     sf___xa_store()/sf___xa_insert()/sf___xa_alloc() (-EINVAL; Linux accepts either)
 */
#ifndef _SF_LINUX_XARRAY_H_
#define _SF_LINUX_XARRAY_H_

#include <linux/xarray.h>

/* Linux: xa_mk_internal(257). What a reserved entry looks like through the raw API. */
#define SF_XA_ZERO_ENTRY	((void *)0x406UL)
/* Linux: XA_PRESENT, the "any entry" filter of xa_find(). */
#define SF_XA_PRESENT		8U

/* Locked by the function itself. */
void	*sf_xa_load(struct xarray *xa, unsigned long index);
void	*sf_xa_find(struct xarray *xa, unsigned long *indexp, unsigned long max,
	    unsigned int filter);
void	*sf_xa_find_after(struct xarray *xa, unsigned long *indexp, unsigned long max,
	    unsigned int filter);
int	 sf_xa_trylock(struct xarray *xa);

/* Caller holds xa_lock. With a blocking gfp the lock is dropped while waiting for memory. */
void	*sf___xa_erase(struct xarray *xa, unsigned long index);
void	*sf___xa_store(struct xarray *xa, unsigned long index, void *entry, gfp_t gfp);
int	 sf___xa_insert(struct xarray *xa, unsigned long index, void *entry, gfp_t gfp);
void	*sf___xa_cmpxchg_raw(struct xarray *xa, unsigned long index, void *old,
	    void *entry, gfp_t gfp);
int	 sf___xa_alloc(struct xarray *xa, uint32_t *id, void *entry, struct xa_limit limit,
	    gfp_t gfp);

#endif
