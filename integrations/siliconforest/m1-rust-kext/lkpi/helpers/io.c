// SPDX-License-Identifier: GPL-2.0
/*
 * SiliconForest replacement for rust/helpers/io.c.
 *
 * Mappings: LinuxKPI's ioremap() ends in sfos_map_phys(..., uncached), an IOKit mapping with
 * kIOMapInhibitCache. On arm64 xnu that is device memory without early write
 * acknowledgement, i.e. what Linux calls non-posted, so ioremap_np() is ioremap() here (and
 * ioremap() is stricter than on Linux, never weaker).
 *
 * Regions: the layer has no resource tree (LinuxKPI's struct resource is start/end/name/
 * flags, no parent or children). __request_region() and release_region() keep a list of
 * what was requested so the Rust `Region` has a real resource to describe and release;
 * they do not detect conflicts, since nothing else in this kernel registers with the list.
 */
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <sf_linux_compat.h>

struct resource iomem_resource = { .start = 0, .end = -1, .name = "PCI mem", .flags = IORESOURCE_MEM };
struct resource ioport_resource = { .start = 0, .end = 0xffff, .name = "PCI IO", .flags = IORESOURCE_IO };

struct sf_region {
	struct resource res;
	struct list_head link;
};

static LINUX_LIST_HEAD(sf_regions);
static DEFINE_MUTEX(sf_regions_lock);

__rust_helper struct resource *
rust_helper___request_region(struct resource *parent, resource_size_t start, resource_size_t n,
			     const char *name, int flags)
{
	struct sf_region *r = kzalloc(sizeof(*r), GFP_KERNEL);

	if (r == NULL)
		return NULL;
	r->res.start = start;
	r->res.end = start + n - 1;
	r->res.name = name;
	r->res.flags = (parent != NULL ? parent->flags : 0) | flags;
	mutex_lock(&sf_regions_lock);
	list_add(&r->link, &sf_regions);
	mutex_unlock(&sf_regions_lock);
	return &r->res;
}

static void
sf_release_region(resource_size_t start, resource_size_t n)
{
	struct sf_region *r, *found = NULL;

	mutex_lock(&sf_regions_lock);
	list_for_each_entry(r, &sf_regions, link) {
		if (r->res.start == start && r->res.end == start + n - 1) {
			found = r;
			list_del(&r->link);
			break;
		}
	}
	mutex_unlock(&sf_regions_lock);
	kfree(found);
}

__rust_helper void rust_helper_release_mem_region(resource_size_t start, resource_size_t n)
{
	sf_release_region(start, n);
}

__rust_helper void rust_helper_release_region(resource_size_t start, resource_size_t n)
{
	sf_release_region(start, n);
}

__rust_helper resource_size_t rust_helper_resource_size(struct resource *res)
{
	return resource_size(res);
}

__rust_helper void __iomem *rust_helper_ioremap(phys_addr_t offset, size_t size)
{
	return ioremap(offset, size);
}

__rust_helper void __iomem *rust_helper_ioremap_np(phys_addr_t offset, size_t size)
{
	return ioremap(offset, size);
}

__rust_helper void rust_helper_iounmap(void __iomem *addr)
{
	iounmap(addr);
}

__rust_helper void *rust_helper_memremap(resource_size_t offset, size_t size, unsigned long flags)
{
	return memremap(offset, size, flags);
}

__rust_helper void rust_helper_memunmap(void *addr)
{
	iounmap(addr);
}

__rust_helper void rust_helper_memcpy_fromio(void *dst, const volatile void __iomem *src, size_t count)
{
	memcpy_fromio(dst, (const void *)src, count);
}

__rust_helper void rust_helper_memcpy_toio(volatile void __iomem *dst, const void *src, size_t count)
{
	memcpy_toio((void *)dst, src, count);
}

#define SF_IO_HELPERS(sfx, type)								\
__rust_helper type rust_helper_read##sfx(const void __iomem *addr)				\
{ return read##sfx(addr); }									\
__rust_helper void rust_helper_write##sfx(type value, void __iomem *addr)			\
{ write##sfx(value, addr); }									\
__rust_helper type rust_helper_read##sfx##_relaxed(const void __iomem *addr)			\
{ return read##sfx##_relaxed(addr); }								\
__rust_helper void rust_helper_write##sfx##_relaxed(type value, void __iomem *addr)		\
{ write##sfx##_relaxed(value, addr); }

SF_IO_HELPERS(b, u8)
SF_IO_HELPERS(w, u16)
SF_IO_HELPERS(l, u32)
SF_IO_HELPERS(q, u64)
