/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Host stand-ins for what linux_radix.c, linux_xarray.c (both taken unmodified from the
 * layer) and ../c/sf_xarray.c include. Only tests/ uses this. Every header name those
 * three files ask for, other than linux/radix-tree.h and linux/xarray.h (the layer's own,
 * copied next to these at build time), is a one-line file here that includes this one.
 */
#ifndef SF_HOST_H
#define SF_HOST_H

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int gfp_t;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/* FreeBSD malloc(9) flags, which LinuxKPI's GFP_* are made of */
#define	M_NOWAIT	0x0001
#define	M_WAITOK	0x0002
#define	M_ZERO		0x0100
#define	GFP_NOWAIT	M_NOWAIT
#define	GFP_KERNEL	M_WAITOK

/* malloc(9): three arguments. Allocation failures are injected by the test. */
void	*sf_host_malloc(size_t size, int flags);
void	 sf_host_free(void *p);
void	 sf_host_vm_wait(void);
#define	MALLOC_DEFINE(type, shortdesc, longdesc)	int type##_unused __attribute__((unused))
#define	malloc(size, type, flags)	sf_host_malloc((size), (flags))
#define	free(p, type)			sf_host_free(p)
#define	vm_wait(obj)			sf_host_vm_wait()

/* a recursive mutex, single-threaded: only the depth is tracked and asserted */
struct mtx { int depth; int initialised; };
#define	MTX_DEF		0
#define	MTX_RECURSE	4
#define	MA_OWNED	1
#define	mtx_init(m, name, type, opts)	((m)->depth = 0, (m)->initialised = 1)
#define	mtx_lock(m)			((void)(assert((m)->initialised), (m)->depth++))
#define	mtx_unlock(m)			((void)(assert((m)->depth > 0), (m)->depth--))
#define	mtx_trylock(m)			((m)->depth++, 1)
#define	mtx_assert(m, what)		assert((m)->depth > 0)
#define	MTX_SYSINIT(name, mtx, desc, opts)

#define	KASSERT(cond, msg)	assert(cond)
#define	MPASS(cond)		assert(cond)
#define	likely(x)		__builtin_expect(!!(x), 1)
#define	unlikely(x)		__builtin_expect(!!(x), 0)
#define	__predict_true(x)	likely(x)
#define	__predict_false(x)	unlikely(x)
#ifndef __unused
#define	__unused		__attribute__((unused))
#endif
#ifndef NBBY
#define	NBBY			8
#endif
#ifndef howmany
#define	howmany(x, y)		(((x) + ((y) - 1)) / (y))
#endif
#ifndef MAX
#define	MAX(a, b)		((a) > (b) ? (a) : (b))
#define	MIN(a, b)		((a) < (b) ? (a) : (b))
#endif
#define	BITS_PER_LONG		64

/* linux/err.h */
#define	MAX_ERRNO	4095
#define	IS_ERR_VALUE(x)	unlikely((uintptr_t)(void *)(x) >= (uintptr_t)-MAX_ERRNO)
static inline void *ERR_PTR(long error) { return ((void *)(intptr_t)error); }
static inline long PTR_ERR(const void *ptr) { return ((intptr_t)ptr); }
static inline bool IS_ERR(const void *ptr) { return (IS_ERR_VALUE((uintptr_t)ptr)); }
static inline int PTR_ERR_OR_ZERO(const void *ptr) { return (IS_ERR(ptr) ? PTR_ERR(ptr) : 0); }

/* linux/bitops.h, as far as the radix tree's tags need */
static inline int test_bit(unsigned bit, const unsigned long *addr)
{ return ((addr[bit / 64] >> (bit % 64)) & 1); }
static inline void set_bit(unsigned bit, unsigned long *addr) { addr[bit / 64] |= 1UL << (bit % 64); }
static inline void clear_bit(unsigned bit, unsigned long *addr) { addr[bit / 64] &= ~(1UL << (bit % 64)); }

#endif
