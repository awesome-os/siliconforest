/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * sf_dcp_iommu.h - Linux's IOMMU and DMA API for the devices behind the display DARTs, in
 * LinuxKPI form, for the pin's drivers/gpu/drm/apple (omacom/linux-aurora 6a08a08e).
 *
 * The layer's <linux/iommu.h> is a stub (struct iommu_domain { type }, a NULL
 * iommu_get_domain_for_dev()) and its DMA API maps identity for a platform device. This
 * header is to be force-included (-include sf_dcp_iommu.h) for every file that does DMA
 * for a display device: the driver sources and the DRM helpers that allocate for the
 * display-subsystem device (drm_gem_dma_helper.c, drm_fbdev_dma.c, drm_fb_dma_helper.c).
 * It redirects the Linux names below to pd_* functions. Each of those acts on the DART
 * when the device is bound to one, and otherwise calls the layer's own implementation, so
 * including it for a file that also handles other devices changes nothing for them.
 *
 * Binding. A device is bound to a stream of a DART
 *   - explicitly: sf_dcp_iommu_bind(dev, "/arm-io/dart-dcp", 0), or
 *   - from its device-tree node, as Linux's of_dma_configure(): the first DMA or IOMMU call
 *     on a device with of_node reads `iommus = <&dart sid ...>`, takes the DART's register
 *     base from that node's `reg` and binds. The nodes m1-gpu-dt publishes for the display
 *     carry these properties.
 *
 * Semantics, per stream, are Linux's for the pin on T8103 with both display DARTs locked
 * (m1n1 locks dart-disp0 before starting Linux, kboot.c:5247):
 *   - addresses handed out are plain stream addresses in the DART's window, as
 *     iommu-dma returns them (no asc-dram-mask bits); sf_dcp_iommu_dva_mask selects m1n1's
 *     form (address | asc-dram-mask) instead, the one boot dc11 used;
 *   - an address the firmware supplies is masked to the stream's 32-bit input size before
 *     the lookup (apple_dart_iova_to_phys, apple-dart.c:1728-1746);
 *   - firmware mappings found in the tables are left exactly as they are: what Linux gets
 *     from the reserved-memory nodes (IOMMU_RESV_TRANSLATED) is here simply still there.
 *
 * Define SF_DCP_IOMMU_NO_REDIRECT before including to get the declarations only.
 */
#ifndef SF_DCP_IOMMU_H
#define SF_DCP_IOMMU_H

#include <linux/types.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/iommu.h>
#include <linux/scatterlist.h>
#include <linux/of.h>

#ifndef IOMMU_READ
#define IOMMU_READ	(1 << 0)
#define IOMMU_WRITE	(1 << 1)
#define IOMMU_CACHE	(1 << 2)
#endif

struct sf_dart_stream;

/*
 * struct iommu_domain as the driver uses it (type, pgsize_bitmap). Files that include this
 * header see this definition under the name `struct iommu_domain`.
 */
struct sf_iommu_domain {
	unsigned int		type;		/* IOMMU_DOMAIN_DMA */
	unsigned long		pgsize_bitmap;	/* SZ_16K */
	struct sf_dart_stream	*sf_stream;
	struct sf_dart_stream	*sf_mirror; /* display-subsystem second IOMMU, optional */
	u64			sf_mask;	/* input-size mask applied to addresses */
};

struct iommu_group;

/* ---- binding ---- */
int sf_dcp_iommu_bind(struct device *dev, const char *dart_path, u32 sid);
/* 1 if the device is (or by its node can be) bound to a DART, 0 if not */
int sf_dcp_iommu_bound(struct device *dev);
/* 0: Linux's address form (default). 1: address | asc-dram-mask of the dcp, m1n1's form. */
extern int sf_dcp_iommu_dva_mask;

/* ---- IOMMU API ---- */
struct sf_iommu_domain *sf_iommu_get_domain_for_dev(struct device *dev);
phys_addr_t sf_iommu_iova_to_phys(struct sf_iommu_domain *domain, dma_addr_t iova);
int sf_iommu_map(struct sf_iommu_domain *domain, unsigned long iova, phys_addr_t paddr,
		 size_t size, int prot, gfp_t gfp);
size_t sf_iommu_unmap(struct sf_iommu_domain *domain, unsigned long iova, size_t size);
ssize_t sf_iommu_map_sgtable(struct sf_iommu_domain *domain, unsigned long iova,
			     struct sg_table *sgt, int prot);
bool sf_iommu_is_dma_domain(struct sf_iommu_domain *domain);
int sf_iommu_device_use_default_domain(struct device *dev);
void sf_iommu_device_unuse_default_domain(struct device *dev);
/* No groups here: one device per stream. Returns NULL; only the H17P paths ask. */
struct iommu_group *sf_iommu_group_get(struct device *dev);
void sf_iommu_group_put(struct iommu_group *group);
int sf_iommu_group_for_each_dev(struct iommu_group *group, void *data,
				int (*fn)(struct device *, void *));

/* of_dma_configure(): binds one or two T8103 `iommus` specs as one domain;
 * malformed/unavailable specs fail closed, never as physical DMA. */
int sf_of_dma_configure(struct device *dev, struct device_node *np, bool force_dma);

/* ---- DMA API ---- */
int sf_dma_set_mask_and_coherent(struct device *dev, u64 mask);
void *sf_dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle, gfp_t gfp);
void sf_dma_free_coherent(struct device *dev, size_t size, void *cpu_addr, dma_addr_t dma_handle);
void *sf_dma_alloc_attrs(struct device *dev, size_t size, dma_addr_t *dma_handle, gfp_t gfp,
			 unsigned long attrs);
void sf_dma_free_attrs(struct device *dev, size_t size, void *cpu_addr, dma_addr_t dma_handle,
		       unsigned long attrs);
int sf_dma_get_sgtable_attrs(struct device *dev, struct sg_table *sgt, void *cpu_addr,
			     dma_addr_t dma_addr, size_t size, unsigned long attrs);
dma_addr_t sf_dma_map_resource(struct device *dev, phys_addr_t phys, size_t size,
			       enum dma_data_direction dir, unsigned long attrs);
void sf_dma_unmap_resource(struct device *dev, dma_addr_t dma, size_t size,
			   enum dma_data_direction dir, unsigned long attrs);
int sf_dma_mapping_error(struct device *dev, dma_addr_t dma);
int sf_dma_map_sgtable(struct device *dev, struct sg_table *sgt, enum dma_data_direction dir,
		       unsigned long attrs);
void sf_dma_unmap_sgtable(struct device *dev, struct sg_table *sgt, enum dma_data_direction dir,
			  unsigned long attrs);

/*
 * For a user mapping of a buffer from sf_dma_alloc_*() (SFDRMDri's sf_drm_mmap_pages
 * resolver for a GEM DMA object, in place of Linux's dma_mmap_wc()/dma_mmap_pages()):
 * writes one physical page number (LinuxKPI PAGE_SHIFT units) per page of `size` into
 * `pages`. The pages stay valid until the buffer is freed, so the caller must keep the
 * owning object alive for the life of the mapping. 0, -ENOENT if cpu_addr is not such a
 * buffer of this device, -EINVAL if size is not whole pages or exceeds the buffer.
 */
int sf_dma_buffer_pages(struct device *dev, void *cpu_addr, size_t size, uint32_t *pages);

/* Counters and the last error of the DART behind a device, one line to the kernel log. */
/* a list of physical pages at one contiguous device address (SFDcp's cross-kext import) */
/* Map failure -EBUSY means backing may remain reachable: dma_handle names
 * its quarantined range; caller MUST retain exporter ref/pin until reboot.
 * Any nonzero unmap result likewise forbids backing/pin release. */
int sf_dma_map_phys_pages(struct device *dev, const u64 *phys, unsigned int n, dma_addr_t *dma_handle);
int sf_dma_unmap_phys_pages(struct device *dev, dma_addr_t dma_handle, unsigned int n);
void sf_dcp_iommu_report(struct device *dev, const char *when);

#ifndef SF_DCP_IOMMU_NO_REDIRECT

#define	iommu_domain				sf_iommu_domain
#define	iommu_get_domain_for_dev(dev)		sf_iommu_get_domain_for_dev(dev)
#define	iommu_iova_to_phys(d, iova)		sf_iommu_iova_to_phys((d), (iova))
#define	iommu_map(d, iova, pa, sz, prot, gfp)	sf_iommu_map((d), (iova), (pa), (sz), (prot), (gfp))
#define	iommu_unmap(d, iova, sz)		sf_iommu_unmap((d), (iova), (sz))
#define	iommu_map_sgtable(d, iova, sgt, prot)	sf_iommu_map_sgtable((d), (iova), (sgt), (prot))
#define	iommu_is_dma_domain(d)			sf_iommu_is_dma_domain(d)
#define	iommu_device_use_default_domain(dev)	sf_iommu_device_use_default_domain(dev)
#define	iommu_device_unuse_default_domain(dev)	sf_iommu_device_unuse_default_domain(dev)
#define	iommu_group_get(dev)			sf_iommu_group_get(dev)
#define	iommu_group_put(g)			sf_iommu_group_put(g)
#define	iommu_group_for_each_dev(g, data, fn)	sf_iommu_group_for_each_dev((g), (data), (fn))
#define	of_dma_configure(dev, np, force)	sf_of_dma_configure((dev), (np), (force))

#define	dma_set_mask_and_coherent(dev, mask)	sf_dma_set_mask_and_coherent((dev), (mask))
#define	dma_alloc_coherent(dev, sz, h, gfp)	sf_dma_alloc_coherent((dev), (sz), (h), (gfp))
#define	dma_free_coherent(dev, sz, va, h)	sf_dma_free_coherent((dev), (sz), (va), (h))
#define	dma_alloc_attrs(dev, sz, h, gfp, a)	sf_dma_alloc_attrs((dev), (sz), (h), (gfp), (a))
#define	dma_free_attrs(dev, sz, va, h, a)	sf_dma_free_attrs((dev), (sz), (va), (h), (a))
#define	dma_alloc_wc(dev, sz, h, gfp)		sf_dma_alloc_attrs((dev), (sz), (h), (gfp), 0)
#define	dma_free_wc(dev, sz, va, h)		sf_dma_free_attrs((dev), (sz), (va), (h), 0)
#define	dma_get_sgtable_attrs(dev, sgt, va, h, sz, a) \
	sf_dma_get_sgtable_attrs((dev), (sgt), (va), (h), (sz), (a))
#define	dma_get_sgtable(dev, sgt, va, h, sz)	sf_dma_get_sgtable_attrs((dev), (sgt), (va), (h), (sz), 0)
#define	dma_map_resource(dev, pa, sz, dir, a)	sf_dma_map_resource((dev), (pa), (sz), (dir), (a))
#define	dma_unmap_resource(dev, h, sz, dir, a)	sf_dma_unmap_resource((dev), (h), (sz), (dir), (a))
#define	dma_mapping_error(dev, h)		sf_dma_mapping_error((dev), (h))
#define	dma_map_sgtable(dev, sgt, dir, a)	sf_dma_map_sgtable((dev), (sgt), (dir), (a))
#define	dma_unmap_sgtable(dev, sgt, dir, a)	sf_dma_unmap_sgtable((dev), (sgt), (dir), (a))

#endif /* !SF_DCP_IOMMU_NO_REDIRECT */

#endif
