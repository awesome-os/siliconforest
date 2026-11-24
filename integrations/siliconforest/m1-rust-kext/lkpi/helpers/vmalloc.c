// SPDX-License-Identifier: GPL-2.0
/*
 * SiliconForest replacement for rust/helpers/vmalloc.c. LinuxKPI has no vrealloc(), and its
 * vmalloc()/vfree() are the heap (linux/slab.h: vfree is kfree). Linux's vmalloc memory
 * always starts on a page, whatever its size, and kernel::alloc::Vmalloc relies on that, so
 * this allocates page-aligned explicitly (malloc_aligned, patches-layer/0004).
 */
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

__rust_helper void *
rust_helper_vrealloc_node_align(const void *p, size_t size, unsigned long align,
				gfp_t flags, int node)
{
	size_t old_size;
	void *n;

	if (align > PAGE_SIZE)
		return NULL;
	if (size == 0) {
		kfree(p);
		return ZERO_SIZE_PTR;
	}
	old_size = ZERO_OR_NULL_PTR(p) ? 0 : ksize(p);
	if (old_size != 0 && size <= old_size)
		return (void *)p;
	n = malloc_aligned(size, PAGE_SIZE, M_KMALLOC, linux_check_m_flags(flags));
	if (n == NULL)
		return NULL;
	if (old_size != 0) {
		memcpy(n, p, old_size);
		kfree(p);
	}
	return n;
}

__rust_helper bool rust_helper_is_vmalloc_addr(const void *x)
{
	return is_vmalloc_addr(x);
}
