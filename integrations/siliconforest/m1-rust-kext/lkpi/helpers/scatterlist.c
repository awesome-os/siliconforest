// SPDX-License-Identifier: GPL-2.0
/* SiliconForest replacement for rust/helpers/scatterlist.c: Linux's file after the headers it
 * relies on linux/scatterlist.h to pull in (dma_addr_t, the DMA mapping calls), plus the
 * scatterlist calls that are exported functions on Linux and static inlines in LinuxKPI. */
#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include "linux-helpers/scatterlist.c"

__rust_helper int rust_helper_sg_alloc_table_from_pages_segment(struct sg_table *sgt,
		struct page **pages, unsigned int n_pages, unsigned int offset,
		unsigned long size, unsigned int max_segment, gfp_t gfp_mask)
{
	return sg_alloc_table_from_pages_segment(sgt, pages, n_pages, offset, size,
						 max_segment, gfp_mask);
}

__rust_helper void rust_helper_sg_free_table(struct sg_table *table)
{
	sg_free_table(table);
}
