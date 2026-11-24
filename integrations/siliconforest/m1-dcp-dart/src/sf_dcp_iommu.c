// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * sf_dcp_iommu.c - Linux's IOMMU and DMA API over sf_dart.h for the devices behind the
 * display DARTs. LinuxKPI side; see sf_dcp_iommu.h for what it stands in for.
 *
 * What it does for a bound device follows Linux at the pin (omacom/linux-aurora 6a08a08e):
 *   dma_alloc_coherent      drivers/iommu/dma-iommu.c iommu_dma_alloc(): pages, a range of
 *                           the stream's address space, a mapping, a kernel view of the pages
 *   dma_get_sgtable         iommu_dma_get_sgtable(): the pages behind such an allocation
 *   dma_map_resource        iommu_dma_map_resource(): a physical range at a new address
 *   iommu_map_sgtable       drivers/iommu/iommu.c: each segment at consecutive addresses
 *   iommu_iova_to_phys      drivers/iommu/apple-dart.c:1728: mask to the input size, look up
 * A device that is not bound gets the layer's own implementation of every call.
 */
#define SF_DCP_IOMMU_NO_REDIRECT
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "sf_dcp_iommu.h"
#include "sf_dart.h"
#include "sf_dart_group.h"
#include "sf_dart_spec.h"
#include "sf_dart_table.h"

#define SF_MAX_BOUND	8
/* the streams' input size on T8103: addresses are masked to 32 bits before a lookup */
#define SF_IOVA_MASK	0xffffffffULL
/* asc-dram-mask of the dcp (iop-dcp-nub): what its firmware ORs onto an address */
#define SF_DCP_DRAM_MASK 0xf00000000ULL

int sf_dcp_iommu_dva_mask;

struct sf_binding {
	struct device *dev;
	int state;	/* 0 free, 1 bound, 2 no DART declared, 3 declared but unavailable */
	struct sf_iommu_domain dom;
};

struct sf_alloc {
	struct list_head link;
	struct device *dev;
	void *cpu;
	struct page **pages;
	unsigned int npages;
	u64 iova;
	bool vmapped;
};

static struct sf_binding sf_bound[SF_MAX_BOUND];
// A declared IOMMU that failed to bind must never fall through to physical
// DMA. A blocked domain routes all calls to fail-closed native operations.
static struct sf_iommu_domain sf_blocked_domain;
static LIST_HEAD(sf_allocs);
static LIST_HEAD(sf_quarantines); // retained backing/device refs until reboot
/* 0: no lock yet, 1: being set up, 2: ready */
static int sf_lock_state;
static struct mutex sf_lock;

static struct mutex *sf_lock_get(void)
{
	if (__sync_bool_compare_and_swap(&sf_lock_state, 0, 1)) {
		mutex_init(&sf_lock);
		WRITE_ONCE(sf_lock_state, 2);
	}
	while (READ_ONCE(sf_lock_state) != 2)
		cpu_relax();
	return &sf_lock;
}

static struct sf_binding *sf_slot(struct device *dev, bool make)
{
	struct sf_binding *free = NULL;
	int i;

	for (i = 0; i < SF_MAX_BOUND; i++) {
		if (sf_bound[i].state && sf_bound[i].dev == dev)
			return &sf_bound[i];
		if (!sf_bound[i].state && !free)
			free = &sf_bound[i];
	}
	if (make && free)
		free->dev = dev;
	return make ? free : NULL;
}

static int sf_bind_streams(struct device *dev, struct sf_dart_stream *s,struct sf_dart_stream *mirror)
{
	struct mutex *lock = sf_lock_get();
	struct sf_binding *b;
	int ret = 0;

	if (!s || s==mirror)
		return -ENODEV;
	mutex_lock(lock);
	b = sf_slot(dev, true);
	if (!b) {
		ret = -ENOSPC;
	} else if (b->state==1) {
		// A live device cannot change domains underneath published DMA.
		ret=b->dom.sf_stream==s && b->dom.sf_mirror==mirror?0:-EBUSY;
	} else {
		b->dom.type = IOMMU_DOMAIN_DMA;
		b->dom.pgsize_bitmap = SF_DART_PAGE;
		b->dom.sf_stream = s;
		b->dom.sf_mirror = mirror;
		b->dom.sf_mask = SF_IOVA_MASK;
		b->state = 1;
	}
	mutex_unlock(lock);
	return ret;
}

int sf_dcp_iommu_bind(struct device *dev, const char *dart_path, u32 sid)
{
	if (!dev || !dart_path)
		return -EINVAL;
	return sf_bind_streams(dev, sf_dart_open(dart_path, 0, sid),NULL);
}

/* `iommus = <&dart sid ...>` of np, the DART found by its node's reg (of_dma_configure) */
int sf_of_dma_configure(struct device *dev,struct device_node *np,bool force_dma)
{
    if(!dev || !np)return -ENODEV;
    int cells=of_property_count_u32_elems(np,"iommus"),consumed=0;
    int specs=of_count_phandle_with_args(np,"iommus","#iommu-cells");
    if(specs<=0)return specs?specs:-ENODEV;
    if(specs>2)return -E2BIG;
    u64 consumer_window[2];const u64 *window=NULL;
    if(of_find_property(np,"apple,dma-range",NULL)) {
        u32 range[4];
        if(of_property_count_u32_elems(np,"apple,dma-range")!=4 ||
           of_property_read_u32_array(np,"apple,dma-range",range,4))return -EINVAL;
        consumer_window[0]=(u64)range[0]<<32|range[1];
        consumer_window[1]=(u64)range[2]<<32|range[3];
        window=consumer_window;
    }
    struct sf_dart_stream *streams[2]={NULL,NULL};
    struct resource resources[2];uint32_t sids[2];
    for(unsigned i=0;i<(unsigned)specs;++i) {
        struct of_phandle_args args={0};struct resource res;
        int ret=of_parse_phandle_with_args(np,"iommus","#iommu-cells",i,&args);
        if(ret || !args.np)return ret?ret:-ENODEV;
        uint32_t sid=0;
        ret=sf_dart_spec(args.args,args.args_count,window,&sid);
        if(!ret)ret=of_address_to_resource(args.np,0,&res);
        consumed+=1+args.args_count;
        of_node_put(args.np);
        if(ret)return ret;
        resources[i]=res;sids[i]=sid;
    }
    // LinuxKPI's count helper stops at a malformed tail. Account for every
    // property cell so it cannot silently omit an incomplete second spec.
    if(consumed!=cells)return -EINVAL;
    for(unsigned i=0;i<(unsigned)specs;++i) {
        streams[i]=sf_dart_open(NULL,resources[i].start,sids[i]);
        if(!streams[i])return -ENODEV;
    }
    return sf_bind_streams(dev,streams[0],streams[1]);
}

/* The device's domain; binds from the device's node on the first call. NULL: not behind a DART. */
static struct sf_iommu_domain *sf_domain(struct device *dev)
{
	struct mutex *lock = sf_lock_get();
	struct sf_binding *b;
	int state;

	if (!dev)
		return NULL;
	mutex_lock(lock);
	b = sf_slot(dev, false);
	state = b ? b->state : 0;
	mutex_unlock(lock);
	if (state == 1)
		return &b->dom;
	if (state == 2)
		return NULL;
	if (state == 3)
		return &sf_blocked_domain;

	if (dev->of_node && sf_of_dma_configure(dev, dev->of_node, true) == 0) {
		mutex_lock(lock);
		b = sf_slot(dev, false);
		mutex_unlock(lock);
		return b && b->state == 1 ? &b->dom : NULL;
	}
	/* No IOMMU property permits the layer's normal DMA API. An existing
	 * property with malformed/unpowered/unavailable resources does not. */
	bool declared=dev->of_node && of_find_property(dev->of_node,"iommus",NULL);
	mutex_lock(lock);
	b = sf_slot(dev, true);
	// Another first caller may have completed the binding while we looked.
	if (b && b->state==1) {
		mutex_unlock(lock);return &b->dom;
	}
	if (b)b->state = declared?3:2;
	mutex_unlock(lock);
	return declared?&sf_blocked_domain:NULL;
}

int sf_dcp_iommu_bound(struct device *dev)
{
	struct sf_iommu_domain *dom=sf_domain(dev);
	return dom && dom->sf_stream;
}

/* the address a device or its firmware is given for a stream address */
static dma_addr_t sf_to_dma(u64 iova)
{
	return sf_dcp_iommu_dva_mask ? (iova | SF_DCP_DRAM_MASK) : iova;
}

// One display DMA address may be visible through both internal and
// external scanout DARTs. Complete all mappings/invalidates before publish.
static int sf_map_deferred(struct sf_iommu_domain *d,u64 iova,u64 phys,u64 bytes,unsigned prot)
{ return sf_dart_group_map_deferred(d->sf_stream,d->sf_mirror,iova,phys,bytes,prot); }
static int sf_flush(struct sf_iommu_domain *d)
{ return sf_dart_group_flush(d->sf_stream,d->sf_mirror); }
static int sf_unmap_checked(struct sf_iommu_domain *d,u64 iova,u64 bytes,u64 *done)
{ return sf_dart_group_unmap_checked(d->sf_stream,d->sf_mirror,iova,bytes,done); }
static u64 sf_unmap(struct sf_iommu_domain *d,u64 iova,u64 bytes)
{ u64 done=0;return sf_unmap_checked(d,iova,bytes,&done)?0:done; }
static int sf_map(struct sf_iommu_domain *d,u64 iova,u64 phys,u64 bytes,unsigned prot)
{ int ret=sf_map_deferred(d,iova,phys,bytes,prot);return ret?ret:sf_flush(d); }
static int sf_iova_alloc(struct sf_iommu_domain *d,u64 bytes,u64 *iova)
{ return sf_dart_iova_alloc_pair(d->sf_stream,d->sf_mirror,bytes,iova); }
static void sf_iova_free(struct sf_iommu_domain *d,u64 iova,u64 bytes)
{ sf_dart_iova_free_pair(d->sf_stream,d->sf_mirror,iova,bytes); }

/* ---- IOMMU API ---- */

struct sf_iommu_domain *sf_iommu_get_domain_for_dev(struct device *dev)
{
	return sf_domain(dev);
}

phys_addr_t sf_iommu_iova_to_phys(struct sf_iommu_domain *domain, dma_addr_t iova)
{
	if (!domain || !domain->sf_stream)
		return 0;
	u64 phys=sf_dart_translate(domain->sf_stream,iova & domain->sf_mask);
	if(domain->sf_mirror && sf_dart_translate(domain->sf_mirror,iova & domain->sf_mask)!=phys)return 0;
	return phys;
}

static unsigned int sf_prot(int prot)
{
	return ((prot & IOMMU_READ) ? SF_DART_PROT_READ : 0) | ((prot & IOMMU_WRITE) ? SF_DART_PROT_WRITE : 0);
}

int sf_iommu_map(struct sf_iommu_domain *domain, unsigned long iova, phys_addr_t paddr,
		 size_t size, int prot, gfp_t gfp)
{
	if (!domain || !domain->sf_stream)
		return -ENODEV;
	return sf_map(domain, iova & domain->sf_mask, paddr, size, sf_prot(prot));
}

size_t sf_iommu_unmap(struct sf_iommu_domain *domain, unsigned long iova, size_t size)
{
	if (!domain || !domain->sf_stream)
		return 0;
	return sf_unmap(domain, iova & domain->sf_mask, size);
}

ssize_t sf_iommu_map_sgtable(struct sf_iommu_domain *domain, unsigned long iova,
			     struct sg_table *sgt, int prot)
{
	struct scatterlist *sg;
	size_t mapped = 0;
	int i, ret;
	bool touched=false;

	if (!domain || !domain->sf_stream || !sgt)
		return -ENODEV;
	iova &= domain->sf_mask;
	for_each_sgtable_sg(sgt, sg, i) {
		phys_addr_t phys = sg_phys(sg);
		size_t len = ALIGN(sg->length, SF_DART_PAGE);

		if (phys & (SF_DART_PAGE - 1)) {
			ret = -EINVAL;
			goto undo;
		}
		touched=true;
		ret = sf_map_deferred(domain, iova + mapped, phys, len, sf_prot(prot));
		if (ret)
			goto undo;
		mapped += len;
	}
	/* one invalidate for the whole table */
	ret = sf_flush(domain);
	if (ret)
		goto undo;
	return mapped;
undo:
	if (mapped)
		sf_unmap(domain, iova, mapped);
	else if (touched)
		sf_flush(domain); // even a failed first map can roll back PTEs
	return ret;
}

bool sf_iommu_is_dma_domain(struct sf_iommu_domain *domain)
{
	return domain && domain->type == IOMMU_DOMAIN_DMA;
}

int sf_iommu_device_use_default_domain(struct device *dev)
{
	/* Linux pins the default DMA domain; here the binding is that domain and never changes */
	struct sf_iommu_domain *dom=sf_domain(dev);
	return dom && dom->sf_stream ? 0 : -ENODEV;
}

void sf_iommu_device_unuse_default_domain(struct device *dev)
{
}

struct iommu_group *sf_iommu_group_get(struct device *dev)
{
	return NULL;
}

void sf_iommu_group_put(struct iommu_group *group)
{
}

int sf_iommu_group_for_each_dev(struct iommu_group *group, void *data,
				int (*fn)(struct device *, void *))
{
	return 0;
}

/* ---- DMA API ---- */

int sf_dma_set_mask_and_coherent(struct device *dev, u64 mask)
{
	/* the stream's 32-bit address range is inside any mask the driver asks for (42 bits) */
	struct sf_iommu_domain *dom=sf_domain(dev);
	if (dom)
		return !dom->sf_stream ? -ENODEV : mask >= SF_IOVA_MASK ? 0 : -EIO;
	return dma_set_mask_and_coherent(dev, mask);
}

static struct sf_alloc *sf_alloc_find(struct device *dev, void *cpu)
{
	struct sf_alloc *a;

	list_for_each_entry(a, &sf_allocs, link)
		if (a->dev == dev && a->cpu == cpu)
			return a;
	return NULL;
}

static void sf_alloc_drop(struct sf_alloc *a, unsigned int have)
{
	unsigned int i;

	for (i = 0; i < have; i++)
		__free_page(a->pages[i]);
	kfree(a->pages);
	kfree(a);
}

static void sf_alloc_quarantine(struct sf_alloc *a)
{
    struct mutex *lock=sf_lock_get();
    get_device(a->dev);
    mutex_lock(lock);list_add(&a->link,&sf_quarantines);mutex_unlock(lock);
    pr_err("SFDcpDart: failed DMA retirement; retaining %u pages and IOVA until reboot\n",a->npages);
}

void *sf_dma_alloc_attrs(struct device *dev, size_t size, dma_addr_t *dma_handle, gfp_t gfp,
			 unsigned long attrs)
{
	struct sf_iommu_domain *dom = sf_domain(dev);
	struct mutex *lock;
	struct sf_alloc *a;
	unsigned int i, n;
	u64 iova;

	if (!dom)
		return dma_alloc_attrs(dev, size, dma_handle, gfp, attrs);
	if (!size || size>UINT32_MAX || !dma_handle || sf_dart_poisoned(dom->sf_stream) ||
	    size>SIZE_MAX-(SF_DART_PAGE-1))
		return NULL;
	n = ALIGN(size, SF_DART_PAGE) >> SF_DART_PAGE_SHIFT;
	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a)
		return NULL;
	a->pages = kcalloc(n, sizeof(*a->pages), GFP_KERNEL);
	if (!a->pages) {
		kfree(a);
		return NULL;
	}
	/* single pages, as iommu-dma: the DART makes them contiguous for the device */
	for (i = 0; i < n; i++) {
		a->pages[i] = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!a->pages[i]) {
			sf_alloc_drop(a, i);
			return NULL;
		}
	}
	if (sf_iova_alloc(dom, (u64)n << SF_DART_PAGE_SHIFT, &iova)) {
		sf_alloc_drop(a, n);
		return NULL;
	}
	a->dev=dev;a->npages=n;a->iova=iova;
	for (i = 0; i < n; i++) {
		if (sf_map_deferred(dom, iova + ((u64)i << SF_DART_PAGE_SHIFT),
					 page_to_phys(a->pages[i]), SF_DART_PAGE,
					 SF_DART_PROT_READ | SF_DART_PROT_WRITE))
			break;
	}
	/* one invalidate for the whole buffer; a failed one counts as a failed mapping */
	int flush_error=sf_flush(dom);
	if (i == n && !flush_error) {
		a->cpu = n == 1 ? page_address(a->pages[0]) : vmap(a->pages, n, VM_MAP, PAGE_KERNEL);
		a->vmapped = n != 1;
	}
	if (i < n || flush_error || !a->cpu) {
		uint64_t unmapped=0;
		// Only complete successful group maps belong to us. The failed
		// peer map may have encountered a foreign PTE; never clear it.
		if (sf_dart_poisoned(dom->sf_stream) ||
		    (i && sf_unmap_checked(dom,iova,(u64)i << SF_DART_PAGE_SHIFT,&unmapped))) {
			sf_alloc_quarantine(a);
			return NULL;
		}
		sf_iova_free(dom, iova, (u64)n << SF_DART_PAGE_SHIFT);
		sf_alloc_drop(a, n);
		return NULL;
	}
	a->dev = dev;
	a->npages = n;
	a->iova = iova;
	lock = sf_lock_get();
	mutex_lock(lock);
	list_add(&a->link, &sf_allocs);
	mutex_unlock(lock);
	*dma_handle = sf_to_dma(iova);
	return a->cpu;
}

void sf_dma_free_attrs(struct device *dev, size_t size, void *cpu_addr, dma_addr_t dma_handle,
		       unsigned long attrs)
{
	struct sf_iommu_domain *dom = sf_domain(dev);
	struct mutex *lock;
	struct sf_alloc *a;

	if (!dom) {
		dma_free_attrs(dev, size, cpu_addr, dma_handle, attrs);
		return;
	}
	lock = sf_lock_get();
	mutex_lock(lock);
	a = sf_alloc_find(dev, cpu_addr);
	if (a)
		list_del(&a->link);
	mutex_unlock(lock);
	if (!a)
		return;
	/* unmap and invalidate first: the device must not reach pages that are being freed */
	uint64_t unmapped=0;
	if (sf_unmap_checked(dom,a->iova,(u64)a->npages << SF_DART_PAGE_SHIFT,&unmapped)) {
		sf_alloc_quarantine(a);
		return;
	}
	sf_iova_free(dom, a->iova, (u64)a->npages << SF_DART_PAGE_SHIFT);
	if (a->vmapped)
		vunmap(a->cpu);
	sf_alloc_drop(a, a->npages);
}

/*
 * Map `n` physical pages, in order, at one contiguous device address of the device's stream
 * (added for SFDcp's import of a buffer another kext owns: the pages are the exporter's and
 * stay pinned by it; nothing is allocated or copied here). One invalidate for the range.
 * Returns 0 and the address, -ENODEV for a device not behind a DART, -ENOMEM/-EIO otherwise.
 */
int sf_dma_map_phys_pages(struct device *dev, const u64 *phys, unsigned int n, dma_addr_t *dma_handle)
{
	struct sf_iommu_domain *dom = sf_domain(dev);
	unsigned int i;
	u64 iova;

	if (!dom)
		return -ENODEV;
	if (!phys || !n || !dma_handle)
		return -EINVAL;
	for (i = 0; i < n; i++)
		if (phys[i] & (SF_DART_PAGE - 1))
			return -EINVAL;
	int allocated=sf_iova_alloc(dom, (u64)n << SF_DART_PAGE_SHIFT, &iova);
	if (allocated)return allocated;
	for (i = 0; i < n; i++) {
		if (sf_map_deferred(dom, iova + ((u64)i << SF_DART_PAGE_SHIFT), phys[i],
					 SF_DART_PAGE, SF_DART_PROT_READ | SF_DART_PROT_WRITE))
			break;
	}
	if (sf_flush(dom) || i < n) {
		uint64_t unmapped=0;
		if (sf_dart_poisoned(dom->sf_stream) ||
		    (i && sf_unmap_checked(dom,iova,(u64)i << SF_DART_PAGE_SHIFT,&unmapped))) {
			*dma_handle=sf_to_dma(iova);
			return -EBUSY; // caller MUST retain its exporter ref and pin
		}
		sf_iova_free(dom, iova, (u64)n << SF_DART_PAGE_SHIFT);
		return -EIO;
	}
	*dma_handle = sf_to_dma(iova);
	return 0;
}

/* Undo sf_dma_map_phys_pages(): unmap, invalidate, give the address range back. */
int sf_dma_unmap_phys_pages(struct device *dev, dma_addr_t dma_handle, unsigned int n)
{
	struct sf_iommu_domain *dom = sf_domain(dev);
	u64 iova;

	if (!dom || !n)return -EINVAL;
	iova = dma_handle & dom->sf_mask;
	uint64_t unmapped=0;
	int ret=sf_unmap_checked(dom,iova,(u64)n << SF_DART_PAGE_SHIFT,&unmapped);
	if(ret)return ret;
	sf_iova_free(dom, iova, (u64)n << SF_DART_PAGE_SHIFT);
	return 0;
}

void *sf_dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle, gfp_t gfp)
{
	return sf_dma_alloc_attrs(dev, size, dma_handle, gfp, 0);
}

void sf_dma_free_coherent(struct device *dev, size_t size, void *cpu_addr, dma_addr_t dma_handle)
{
	sf_dma_free_attrs(dev, size, cpu_addr, dma_handle, 0);
}

int sf_dma_get_sgtable_attrs(struct device *dev, struct sg_table *sgt, void *cpu_addr,
			     dma_addr_t dma_addr, size_t size, unsigned long attrs)
{
	struct mutex *lock;
	struct scatterlist *sg;
	struct sf_alloc *a;
	unsigned int i, n;
	int ret;

	if (!sf_domain(dev))
		return -ENXIO; /* the layer has no dma_get_sgtable for an unbound device */
	lock = sf_lock_get();
	mutex_lock(lock);
	a = sf_alloc_find(dev, cpu_addr);
	mutex_unlock(lock);
	if (!a)
		return -EINVAL;
	n = min_t(unsigned int, a->npages, ALIGN(size, SF_DART_PAGE) >> SF_DART_PAGE_SHIFT);
	ret = sg_alloc_table(sgt, n, GFP_KERNEL);
	if (ret)
		return ret;
	for_each_sgtable_sg(sgt, sg, i)
		sg_set_page(sg, a->pages[i], SF_DART_PAGE, 0);
	return 0;
}

dma_addr_t sf_dma_map_resource(struct device *dev, phys_addr_t phys, size_t size,
			       enum dma_data_direction dir, unsigned long attrs)
{
	struct sf_iommu_domain *dom = sf_domain(dev);
	u64 off = phys & (SF_DART_PAGE - 1), len = ALIGN(off + size, SF_DART_PAGE), iova;

	if (!dom)
		return dma_map_resource(dev, phys, size, dir, attrs);
	if (!size || sf_iova_alloc(dom, len, &iova))
		return DMA_MAPPING_ERROR;
	if (sf_map(dom, iova, phys - off, len, SF_DART_PROT_READ | SF_DART_PROT_WRITE)) {
		sf_iova_free(dom, iova, len);
		return DMA_MAPPING_ERROR;
	}
	return sf_to_dma(iova + off);
}

void sf_dma_unmap_resource(struct device *dev, dma_addr_t dma, size_t size,
			   enum dma_data_direction dir, unsigned long attrs)
{
	struct sf_iommu_domain *dom = sf_domain(dev);
	u64 iova, off, len;

	if (!dom) {
		dma_unmap_resource(dev, dma, size, dir, attrs);
		return;
	}
	iova = dma & dom->sf_mask;
	off = iova & (SF_DART_PAGE - 1);
	len = ALIGN(off + size, SF_DART_PAGE);
	sf_unmap(dom, iova - off, len);
	sf_iova_free(dom, iova - off, len);
}

int sf_dma_mapping_error(struct device *dev, dma_addr_t dma)
{
	if (sf_domain(dev))
		return dma == DMA_MAPPING_ERROR;
	return dma_mapping_error(dev, dma);
}

/* A table of whole pages at consecutive addresses; one segment's address covers the lot. */
int sf_dma_map_sgtable(struct device *dev, struct sg_table *sgt, enum dma_data_direction dir,
		       unsigned long attrs)
{
	struct sf_iommu_domain *dom = sf_domain(dev);
	struct scatterlist *sg;
	u64 total = 0, iova, at = 0;
	int i;

	if (!dom)
		return dma_map_sgtable(dev, sgt, dir, attrs);
	for_each_sgtable_sg(sgt, sg, i) {
		if (sg->offset || (sg->length & (SF_DART_PAGE - 1)))
			return -EINVAL;
		total += sg->length;
	}
	if (!total || sf_iova_alloc(dom, total, &iova))
		return -ENOMEM;
	for_each_sgtable_sg(sgt, sg, i) {
		if (sf_map_deferred(dom, iova + at, sg_phys(sg), sg->length,
					 SF_DART_PROT_READ | SF_DART_PROT_WRITE)) {
			if (at)sf_unmap(dom, iova, at);
			else sf_flush(dom);
			sf_iova_free(dom, iova, total);
			return -EIO;
		}
		sg_dma_address(sg) = sf_to_dma(iova + at);
		sg_dma_len(sg) = sg->length;
		at += sg->length;
	}
	if (sf_flush(dom)) {
		sf_unmap(dom, iova, total);
		sf_iova_free(dom, iova, total);
		return -EIO;
	}
	sgt->nents = sgt->orig_nents;
	return 0;
}

void sf_dma_unmap_sgtable(struct device *dev, struct sg_table *sgt, enum dma_data_direction dir,
			  unsigned long attrs)
{
	struct sf_iommu_domain *dom = sf_domain(dev);
	struct scatterlist *sg;
	u64 total = 0, iova;
	int i;

	if (!dom) {
		dma_unmap_sgtable(dev, sgt, dir, attrs);
		return;
	}
	if (!sgt->sgl)
		return;
	iova = sg_dma_address(sgt->sgl) & dom->sf_mask;
	for_each_sgtable_sg(sgt, sg, i)
		total += sg->length;
	sf_unmap(dom, iova, total);
	sf_iova_free(dom, iova, total);
}

int sf_dma_buffer_pages(struct device *dev, void *cpu_addr, size_t size, uint32_t *pages)
{
	struct mutex *lock = sf_lock_get();
	struct sf_alloc *a;
	unsigned int i, n = size >> SF_DART_PAGE_SHIFT;
	int ret = 0;

	if (!pages || !size || (size & (SF_DART_PAGE - 1)))
		return -EINVAL;
	mutex_lock(lock);
	a = sf_alloc_find(dev, cpu_addr);
	if (!a)
		ret = -ENOENT;
	else if (n > a->npages)
		ret = -EINVAL;
	else
		for (i = 0; i < n; i++)
			pages[i] = (uint32_t)(page_to_phys(a->pages[i]) >> PAGE_SHIFT);
	mutex_unlock(lock);
	return ret;
}

void sf_dcp_iommu_report(struct device *dev, const char *when)
{
	struct sf_iommu_domain *dom = sf_domain(dev);

	if (dom) {
		sf_dart_check(dom->sf_stream, when);
		if(dom->sf_mirror)sf_dart_check(dom->sf_mirror,when);
	}
}
