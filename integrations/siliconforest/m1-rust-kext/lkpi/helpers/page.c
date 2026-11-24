// SPDX-License-Identifier: GPL-2.0
/*
 * SiliconForest replacement for rust/helpers/page.c.
 *
 * LinuxKPI's struct page is FreeBSD's vm_page, and on SiliconForest a vm_page exists only for
 * memory the layer itself allocated (sf_linux_page.c keeps them in a table keyed by physical
 * address). There is no memmap: pfn_valid() means "this layer has a struct page for it",
 * and page_is_ram() can only say yes for those pages. One memory node.
 */
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/pfn.h>

__rust_helper struct page *rust_helper_alloc_pages(gfp_t gfp_mask, unsigned int order)
{
	return alloc_pages(gfp_mask, order);
}

__rust_helper void rust_helper___free_pages(struct page *page, unsigned int order)
{
	__free_pages(page, order);
}

__rust_helper void *rust_helper_kmap_local_page(struct page *page)
{
	return kmap_local_page(page);
}

__rust_helper void rust_helper_kunmap_local(const void *addr)
{
	kunmap_local((void *)addr);
}

__rust_helper int rust_helper_page_to_nid(const struct page *page)
{
	return 0;
}

__rust_helper struct page *rust_helper_phys_to_page(phys_addr_t phys)
{
	return PHYS_TO_VM_PAGE(phys);
}

__rust_helper phys_addr_t rust_helper_page_to_phys(struct page *page)
{
	return page_to_phys(page);
}

__rust_helper unsigned long rust_helper_phys_to_pfn(phys_addr_t phys)
{
	return PHYS_PFN(phys);
}

__rust_helper struct page *rust_helper_pfn_to_page(unsigned long pfn)
{
	return pfn_to_page(pfn);
}

__rust_helper bool rust_helper_pfn_valid(unsigned long pfn)
{
	return pfn_to_page(pfn) != NULL;
}

__rust_helper int rust_helper_page_is_ram(unsigned long pfn)
{
	return pfn_to_page(pfn) != NULL;
}

__rust_helper struct page *rust_helper_vmalloc_to_page(const void *addr)
{
	return vmalloc_to_page(addr);
}
