// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * sf_dcp_mmap.c - user mappings of the display driver's GEM objects (dumb buffers).
 *
 * /dev/dri mmap on SiliconForest does not go through file_operations.mmap: SFDRMDri asks the
 * DRM kext once, at mmap time, for the physical pages behind an offset (sf_drm_mmap_pages).
 * The driver's objects are DMA GEM objects allocated through the DART backend, which hands
 * out the pages behind a buffer (sf_dma_buffer_pages, m1-dcp-dart). This is that agent's
 * resolver (ANSWERS.md), modelled on m1-drm72's drm_gem_shmem_sf_mmap_pages(): same lookup,
 * same rule that the mapping holds an object reference until SFDRMDri reports it gone.
 */
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_vma_manager.h>
#include <linux/fs.h>
#include <linux/workqueue.h>

void kprintf(const char *, ...);

/* xnu WIMG cache modes, the values m1-drm72's shmem resolver passes (drm_gem_shmem_helper.c:44) */
#define SF_DCP_WIMG_DEFAULT	0x2
#define SF_DCP_WIMG_WCOMB	0x6

struct sf_dcp_unmap {
	struct work_struct work;
	struct drm_gem_object *obj;
};

static void
sf_dcp_dma_unmap_work(struct work_struct *work)
{
	struct sf_dcp_unmap *u = container_of(work, struct sf_dcp_unmap, work);

	drm_gem_object_put(u->obj);
	kfree(u);
}

/* SFDRMDri calls this when the last mapping is gone, possibly from the pager's context */
static void
sf_dcp_dma_unmap(void *arg)
{
	struct sf_dcp_unmap *u = arg;

	schedule_work(&u->work);
}

int
sf_dcp_gem_dma_mmap_pages(struct linux_file *filp, off_t offset, size_t size, int prot,
    uint32_t *pages, unsigned int *wimg, void (**release)(void *), void **release_arg)
{
	struct drm_file *priv = filp->private_data;
	struct drm_device *dev = priv->minor->dev;
	struct drm_vma_offset_node *node;
	struct drm_gem_object *obj = NULL;
	struct drm_gem_dma_object *dma_obj;
	struct sf_dcp_unmap *u;
	int ret;

	if (size == 0 || (size & (PAGE_SIZE - 1)) != 0 || (offset & (PAGE_SIZE - 1)) != 0)
		return -EINVAL;
	drm_vma_offset_lock_lookup(dev->vma_offset_manager);
	node = drm_vma_offset_exact_lookup_locked(dev->vma_offset_manager, offset >> PAGE_SHIFT,
	    size >> PAGE_SHIFT);
	if (node != NULL) {
		obj = container_of(node, struct drm_gem_object, vma_node);
		if (!kref_get_unless_zero(&obj->refcount))
			obj = NULL;
	}
	drm_vma_offset_unlock_lookup(dev->vma_offset_manager);
	if (obj == NULL) {
		kprintf("SFDcp: mmap: no GEM object at offset 0x%llx size 0x%zx\n", (unsigned long long)offset, size);
		return -EINVAL;
	}
	dma_obj = to_drm_gem_dma_obj(obj);
	u = kzalloc(sizeof(*u), GFP_KERNEL);
	if (u == NULL)
		ret = -ENOMEM;
	else if (!drm_vma_node_is_allowed(node, priv))
		ret = -EACCES;
	else if (drm_gem_is_imported(obj) || size > obj->size || dma_obj->vaddr == NULL)
		ret = -EINVAL;
	else
		ret = sf_dma_buffer_pages(drm_dev_dma_dev(dev), dma_obj->vaddr, size, pages);
	if (ret) {
		kprintf("SFDcp: mmap: offset 0x%llx size 0x%zx refused: %d\n", (unsigned long long)offset, size, ret);
		kfree(u);
		drm_gem_object_put(obj);
		return ret;
	}
	INIT_WORK(&u->work, sf_dcp_dma_unmap_work);
	u->obj = obj;
	*wimg = SF_DCP_WIMG_WCOMB;	/* Linux maps these write-combined (dma_mmap_wc) */
	*release = sf_dcp_dma_unmap;
	*release_arg = u;
	return 0;
}

/*
 * dma_mmap_wc() as drm_gem_dma_mmap() calls it (sf_dcp_compat.h). The only mmap that reaches
 * it is the one of a dumb buffer's dma-buf descriptor: the layer walks dma_buf_mmap_internal
 * -> drm_gem_dmabuf_mmap -> drm_gem_prime_mmap -> drm_gem_dma_object_mmap with a vma that
 * asks for the pages (struct sf_vma_pages, linux/mm.h). drm_gem_dma_mmap() has made
 * vm_pgoff relative to the buffer. The object reference drm_gem_prime_mmap() took is the
 * mapping's; drm_gem_dma_vm_ops.close (drm_gem_vm_close) drops it.
 */
int
sf_dcp_dma_mmap_wc(struct device *dev, struct vm_area_struct *vma, void *cpu_addr, size_t size)
{
	struct sf_vma_pages *rq = vma->sf_pages;
	size_t first = vma->vm_pgoff, n = size >> PAGE_SHIFT;
	uint32_t *all;
	int ret;

	if (rq == NULL)
		return -ENODEV;
	if (cpu_addr == NULL || n == 0)
		return -EINVAL;
	if (first == 0)
		ret = sf_dma_buffer_pages(dev, cpu_addr, size, rq->pages);
	else {
		/* the DART backend lists a buffer from its start */
		all = kvmalloc_array(first + n, sizeof(*all), GFP_KERNEL);
		if (all == NULL)
			return -ENOMEM;
		ret = sf_dma_buffer_pages(dev, cpu_addr, (first + n) << PAGE_SHIFT, all);
		if (ret == 0)
			memcpy(rq->pages, all + first, n * sizeof(*all));
		kvfree(all);
	}
	if (ret)
		return ret;
	rq->wimg = SF_DCP_WIMG_WCOMB;	/* as linux: dma_mmap_wc */
	rq->filled = true;
	return 0;
}

/*
 * mode_config_funcs.fb_create. Linux 6.17 added the format info as a third parameter; the
 * layer's DRM core (6.14) calls fb_create(dev, file, mode_cmd). The driver names the pin's
 * four-argument drm_gem_fb_create() there, which then took mode_cmd for the format info
 * and garbage for mode_cmd (boot ft02: NULL dereference in drm_gem_fb_init_with_funcs).
 * apple_drv.c is compiled with drm_gem_fb_create renamed to this (kext/CMakeLists.txt).
 */
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_framebuffer_helper.h>

struct drm_framebuffer *
sf_dcp_gem_fb_create(struct drm_device *dev, struct drm_file *file, const struct drm_mode_fb_cmd2 *mode_cmd)
{
	const struct drm_format_info *info = drm_get_format_info(dev, mode_cmd);

	if (info == NULL) {
		kprintf("SFDcp: fb_create: no format info for fourcc 0x%08x\n", mode_cmd->pixel_format);
		return ERR_PTR(-EINVAL);
	}
	return drm_gem_fb_create(dev, file, info, mode_cmd);
}
