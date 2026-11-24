// SPDX-License-Identifier: GPL-2.0
/*
 * SiliconForest replacement for rust/helpers/slab.c.
 *
 * Linux 7.2 has krealloc_node_align()/kvrealloc_node_align(); LinuxKPI (6.14 level) has
 * krealloc()/kvrealloc() without alignment or node. These implement the 7.2 contract on the
 * layer's allocator (patches-layer/0004):
 *   - kmalloc() memory up to a page is aligned to the next power of two of its size, above
 *     a page to the page; the Rust side always asks for a size that is a multiple of the
 *     alignment, so any alignment up to PAGE_SIZE is met. It is checked, not assumed.
 *   - ksize() is the size that was asked for, so "the contents up to the lesser of the old
 *     and new sizes are kept" and "__GFP_ZERO zeroes what is new" hold exactly.
 *   - GFP_ATOMIC/GFP_NOWAIT never block; they fail above a page (kmalloc) or above the
 *     kalloc zone limit (kvmalloc).
 * A block is reused in place only when it is big enough and already aligned.
 */
#include <linux/slab.h>
#include <linux/string.h>

static void *
sf_realloc_aligned(const void *objp, size_t new_size, unsigned long align, gfp_t flags, bool kv)
{
	size_t old_size;
	void *p;

	if (align > PAGE_SIZE || (align & (align - 1)) != 0)
		return NULL;
	if (new_size == 0) {
		kfree(objp);
		return ZERO_SIZE_PTR;
	}
	old_size = ZERO_OR_NULL_PTR(objp) ? 0 : ksize(objp);
	if (old_size != 0 && new_size <= old_size && ((uintptr_t)objp & (align - 1)) == 0)
		return (void *)objp;

	p = kv ? kvmalloc(new_size, flags) : kmalloc(new_size, flags);
	if (p == NULL)
		return NULL;
	if (((uintptr_t)p & (align - 1)) != 0) {
		/* the size was not a multiple of the alignment: not a layout Rust produces */
		kfree(p);
		return NULL;
	}
	if (old_size != 0) {
		memcpy(p, objp, old_size < new_size ? old_size : new_size);
		kfree(objp);
	}
	return p;
}

__rust_helper void *
rust_helper_krealloc_node_align(const void *objp, size_t new_size, unsigned long align,
				gfp_t flags, int node)
{
	return sf_realloc_aligned(objp, new_size, align, flags, false);
}

__rust_helper void *
rust_helper_kvrealloc_node_align(const void *p, size_t size, unsigned long align,
				 gfp_t flags, int node)
{
	return sf_realloc_aligned(p, size, align, flags, true);
}
