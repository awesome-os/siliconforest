// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * sf_dcp_prime.c - importing a buffer that another DRM kext exported (SFAgx's render node),
 * for scanout, without a copy.
 *
 * Every DRM kext links its own copy of the layer and the DRM core. Linux's import
 * (dma_buf_attach + map_attachment) would run the exporter's copy of the DMA code on this
 * kext's device, which knows nothing of the display DART. So for a dma-buf whose GEM object
 * belongs to another device:
 *   - the exporter's GEM object (dma_buf->priv, same struct layout: same layer sources) is
 *     pinned through its own funcs->pin under its reservation lock, and asked for its page
 *     table (funcs->get_sg_table): pages and a pin, no CPU mapping;
 *   - the pages are mapped at one contiguous address of this device's DART stream
 *     (sf_dma_map_phys_pages, m1-dcp-dart);
 *   - a DMA GEM object with that address and no CPU mapping is made with the pin's own
 *     drm_gem_dma_prime_import_sg_table() from a one-entry table.
 * The object holds a reference on the dma-buf and the exporter's pin until it is freed.
 * A dma-buf of this device takes Linux's normal path (the same object comes back).
 * Compiled with the driver's forced includes.
 */
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_prime.h>
#include <linux/dma-buf.h>
#include <linux/dma-resv.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

void kprintf(const char *, ...);

struct sf_dcp_import {
	struct list_head link;
	struct drm_gem_object *obj;		/* ours */
	struct drm_gem_object_funcs funcs;	/* the pin's, with .free replaced */
	const struct drm_gem_object_funcs *orig;
	struct dma_buf *dmabuf;			/* reference held */
	struct drm_gem_object *exp;		/* the exporter's object, pinned */
	struct device *dma_dev;
	dma_addr_t dma;
	unsigned int npages;
	u64 first_phys, last_phys;
};

static LIST_HEAD(sf_dcp_imports);
static LIST_HEAD(sf_dcp_import_quarantines); // pins + dma-buf refs retained until reboot
static DEFINE_MUTEX(sf_dcp_import_lock);

static void
sf_dcp_import_unpin(struct drm_gem_object *exp)
{
	if (exp->funcs != NULL && exp->funcs->unpin != NULL) {
		dma_resv_lock(exp->resv, NULL);
		exp->funcs->unpin(exp);
		dma_resv_unlock(exp->resv);
	}
}

static void
sf_dcp_import_quarantine(struct sf_dcp_import *imp)
{
    mutex_lock(&sf_dcp_import_lock);
    list_add(&imp->link,&sf_dcp_import_quarantines);
    mutex_unlock(&sf_dcp_import_lock);
    kprintf("SFDcp: prime: DART retirement failed; retaining %u exporter pages/ref/pin until reboot\n",imp->npages);
}

static void
sf_dcp_import_free(struct drm_gem_object *obj)
{
	struct sf_dcp_import *imp = NULL, *it;
	struct drm_gem_dma_object *dma_obj = to_drm_gem_dma_obj(obj);

	mutex_lock(&sf_dcp_import_lock);
	list_for_each_entry(it, &sf_dcp_imports, link) {
		if (it->obj == obj) {
			imp = it;
			list_del(&it->link);
			break;
		}
	}
	mutex_unlock(&sf_dcp_import_lock);
	if (imp == NULL) {
		kprintf("SFDcp: prime: freeing an import without a record\n");
		return;
	}
	/* the DART's state (error register included) while the mapping still exists */
	sf_dcp_iommu_report(imp->dma_dev, "before an imported buffer is released");
	/* the display must not reach the pages once the exporter may reuse them */
	if (sf_dma_unmap_phys_pages(imp->dma_dev, imp->dma, imp->npages)) {
		sf_dcp_import_quarantine(imp);
		return;
	}
	sf_dcp_import_unpin(imp->exp);
	dma_buf_put(imp->dmabuf);
	if (dma_obj->sgt != NULL) {
		sg_free_table(dma_obj->sgt);
		kfree(dma_obj->sgt);
		dma_obj->sgt = NULL;
	}
	obj->funcs = imp->orig;
	kfree(imp);
	drm_gem_dma_free(dma_obj);
}

struct drm_gem_object *
sf_dcp_prime_import(struct drm_device *dev, struct dma_buf *dma_buf)
{
	struct drm_gem_object *exp = dma_buf->priv, *obj;
	struct dma_buf_attachment attach = { .dmabuf = dma_buf };
	struct sf_dcp_import *imp;
	struct sg_table *esgt, *sgt = NULL;
	struct scatterlist *sg;
	unsigned int npages = dma_buf->size >> PAGE_SHIFT, n = 0, i;
	u64 *phys = NULL;
	int ret;

	/* one of ours: Linux's path hands the same object back */
	if (exp != NULL && exp->dev == dev)
		return drm_gem_prime_import(dev, dma_buf);

	if (exp == NULL || exp->funcs == NULL || exp->funcs->get_sg_table == NULL || npages == 0 || dma_buf->size>UINT32_MAX ||
	    (dma_buf->size & (PAGE_SIZE - 1)) != 0) {
		kprintf("SFDcp: prime: dma-buf of %zu bytes cannot be imported (no page table from its exporter)\n",
		    dma_buf->size);
		return ERR_PTR(-EINVAL);
	}
	imp = kzalloc(sizeof(*imp), GFP_KERNEL);
	phys = kvmalloc_array(npages, sizeof(*phys), GFP_KERNEL);
	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (imp == NULL || phys == NULL || sgt == NULL) {
		ret = -ENOMEM;
		goto err_free;
	}

	/* pin in the exporter, then its pages */
	dma_resv_lock(exp->resv, NULL);
	ret = exp->funcs->pin != NULL ? exp->funcs->pin(exp) : 0;
	dma_resv_unlock(exp->resv);
	if (ret) {
		kprintf("SFDcp: prime: the exporter's pin failed: %d\n", ret);
		goto err_free;
	}
	esgt = exp->funcs->get_sg_table(exp);
	if (IS_ERR_OR_NULL(esgt)) {
		ret = esgt == NULL ? -ENOMEM : PTR_ERR(esgt);
		kprintf("SFDcp: prime: the exporter's get_sg_table failed: %d\n", ret);
		goto err_unpin;
	}
	for_each_sg(esgt->sgl, sg, esgt->orig_nents, i) {
		u64 base = sg_phys(sg);
		unsigned int off;

		for (off = 0; off < sg->length && n < npages; off += PAGE_SIZE)
			phys[n++] = base + off;
	}
	sg_free_table(esgt);
	kfree(esgt);
	if (n != npages) {
		kprintf("SFDcp: prime: exporter's table has %u pages, the buffer %u\n", n, npages);
		ret = -EINVAL;
		goto err_unpin;
	}

	imp->dma_dev = drm_dev_dma_dev(dev);
	get_dma_buf(dma_buf);
	imp->dmabuf=dma_buf;imp->exp=exp;imp->npages=npages;
	ret = sf_dma_map_phys_pages(imp->dma_dev, phys, npages, &imp->dma);
	if (ret) {
		kprintf("SFDcp: prime: mapping %u pages for the display failed: %d\n", npages, ret);
		if (ret==-EBUSY)goto err_quarantine;
		goto err_unpin;
	}

	/* the pin's importer, given the one contiguous range the DART made */
	ret = sg_alloc_table(sgt, 1, GFP_KERNEL);
	if (ret)
		goto err_unmap;
	sg_dma_address(sgt->sgl) = imp->dma;
	sg_dma_len(sgt->sgl) = dma_buf->size;
	sgt->sgl->length = dma_buf->size;
	obj = drm_gem_dma_prime_import_sg_table(dev, &attach, sgt);
	if (IS_ERR(obj)) {
		ret = PTR_ERR(obj);
		kprintf("SFDcp: prime: drm_gem_dma_prime_import_sg_table failed: %d\n", ret);
		sg_free_table(sgt);
		goto err_unmap;
	}

	imp->obj = obj;
	imp->dmabuf = dma_buf;
	imp->exp = exp;
	imp->npages = npages;
	imp->orig = obj->funcs;
	imp->funcs = *obj->funcs;
	imp->funcs.free = sf_dcp_import_free;
	obj->funcs = &imp->funcs;
	mutex_lock(&sf_dcp_import_lock);
	list_add(&imp->link, &sf_dcp_imports);
	mutex_unlock(&sf_dcp_import_lock);
	imp->first_phys = phys[0];
	imp->last_phys = phys[npages - 1];
	{
		/*
		 * What was mapped, checked against the DART itself: every page's device address
		 * is translated back through the tables and compared with the exporter's page.
		 * (Boot pr01's line printed the device address under the label "phys".)
		 */
		struct iommu_domain *dom = iommu_get_domain_for_dev(imp->dma_dev);
		unsigned int wrong = 0;
		u64 hash = 1469598103934665603ULL;
		static int said;

		for (i = 0; i < npages; i++) {
			u64 back = dom != NULL ? iommu_iova_to_phys(dom, imp->dma + ((u64)i << PAGE_SHIFT)) : 0;

			hash = (hash ^ (phys[i] >> PAGE_SHIFT)) * 1099511628211ULL;
			if (back != phys[i])
				wrong++;
		}
		if (said++ < 8 || wrong != 0)
			kprintf("SFDcp: prime: imported %u pages at display address 0x%llx: exporter pages phys "
			    "0x%llx .. 0x%llx (page-frame hash %016llx); DART translates the first back to 0x%llx; "
			    "%u of %u pages translate wrongly%s\n", npages, (unsigned long long)imp->dma,
			    (unsigned long long)phys[0], (unsigned long long)phys[npages - 1],
			    (unsigned long long)hash,
			    (unsigned long long)(dom != NULL ? iommu_iova_to_phys(dom, imp->dma) : 0), wrong, npages,
			    wrong != 0 ? " FAILED" : "");
	}
	kvfree(phys);
	return obj;

err_unmap:
	if (sf_dma_unmap_phys_pages(imp->dma_dev, imp->dma, npages))goto err_quarantine;
err_unpin:
	sf_dcp_import_unpin(exp);
	if (imp->dmabuf)dma_buf_put(imp->dmabuf);
err_free:
	kfree(sgt);
	kvfree(phys);
	kfree(imp);
	return ERR_PTR(ret);
err_quarantine:
	sf_dcp_import_quarantine(imp);
	kfree(sgt);kvfree(phys);
	return ERR_PTR(-EIO);
}
