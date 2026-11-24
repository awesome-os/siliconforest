// SPDX-License-Identifier: GPL-2.0
/*
 * SiliconForest replacement for rust/helpers/xarray.c.
 *
 * rust/kernel/xarray.rs is used unmodified. Every xarray function it calls is routed here,
 * and from here to Linux-semantics implementations over LinuxKPI's `struct xarray`
 * (sf_xarray.c in the layer; linux/sf_xarray.h says why LinuxKPI's own functions do not
 * do). The LinuxKPI functions of the same names take a 32-bit index and differ in error
 * codes, so they are kept out of the bindings (bindgen/bindgen_parameters_add) and the
 * helpers below carry Linux's signatures.
 *
 * Not routed: xa_init_flags() and xa_destroy(), which are LinuxKPI's own functions, found
 * by bindgen. XA_FLAGS_ALLOC/XA_FLAGS_ALLOC1 are LinuxKPI's values, which is what its
 * xa_init_flags() expects.
 */
#include <linux/sf_xarray.h>

__rust_helper void *rust_helper_xa_zero_entry(void)
{
	return SF_XA_ZERO_ENTRY;
}

__rust_helper int rust_helper_xa_err(void *entry)
{
	return xa_err(entry);
}

__rust_helper int rust_helper_xa_trylock(struct xarray *xa)
{
	return sf_xa_trylock(xa);
}

__rust_helper void rust_helper_xa_lock(struct xarray *xa)
{
	xa_lock(xa);
}

__rust_helper void rust_helper_xa_unlock(struct xarray *xa)
{
	xa_unlock(xa);
}

__rust_helper void *rust_helper_xa_load(struct xarray *xa, unsigned long index)
{
	return sf_xa_load(xa, index);
}

__rust_helper void *rust_helper_xa_find(struct xarray *xa, unsigned long *indexp,
					unsigned long max, unsigned int filter)
{
	return sf_xa_find(xa, indexp, max, filter);
}

__rust_helper void *rust_helper_xa_find_after(struct xarray *xa, unsigned long *indexp,
					      unsigned long max, unsigned int filter)
{
	return sf_xa_find_after(xa, indexp, max, filter);
}

__rust_helper void *rust_helper___xa_erase(struct xarray *xa, unsigned long index)
{
	return sf___xa_erase(xa, index);
}

__rust_helper void *rust_helper___xa_store(struct xarray *xa, unsigned long index,
					   void *entry, gfp_t gfp)
{
	return sf___xa_store(xa, index, entry, gfp);
}

__rust_helper int rust_helper___xa_insert(struct xarray *xa, unsigned long index,
					  void *entry, gfp_t gfp)
{
	return sf___xa_insert(xa, index, entry, gfp);
}

__rust_helper void *rust_helper___xa_cmpxchg_raw(struct xarray *xa, unsigned long index,
						 void *old, void *entry, gfp_t gfp)
{
	return sf___xa_cmpxchg_raw(xa, index, old, entry, gfp);
}

__rust_helper int rust_helper___xa_alloc(struct xarray *xa, u32 *id, void *entry,
					 struct xa_limit limit, gfp_t gfp)
{
	return sf___xa_alloc(xa, id, entry, limit, gfp);
}
