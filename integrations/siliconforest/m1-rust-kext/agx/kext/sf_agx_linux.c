// SPDX-License-Identifier: GPL-2.0
/*
 * sf_agx_linux.c - SFAgx: what the Asahi driver finds in a Linux device tree and kernel
 * and does not find on SiliconForest, supplied from the LinuxKPI side without touching the
 * driver.
 *
 *  - the device, right before the driver's probe (the layer's sf_platform_pre_probe):
 *    the RTKit port gets its IOService, GFX is checked to be powered (the driver's first
 *    act is a register write in the SGX window; SFGpuDT powered the domain before it
 *    published the node this kext matches, this is the check that it still is), and the
 *    GPU's mailbox is named (Apple's tree has no mboxes phandle).
 *  - drivers/gpu/drm/asahi/sysfs.c: device attributes. The layer has no sysfs
 *    (device_create_file() fails with -ENOMEM: its sysctl backing is absent), and the
 *    driver's probe fails when registration fails. SF_STUB below.
 */
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <linux/gfp.h>
#include <linux/ktime.h>
#include <linux/compat.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/vmalloc.h>

#include "sf_rtkit_platform.h"
#include "sf_rtkit_compat.h"

void kprintf(const char *, ...);
extern int (*sf_platform_pre_probe)(struct platform_device *pdev);
extern int (*sf_driver_vsnprintf)(char *, size_t, const char *, va_list);
int sf_drm_vsnprintf(char *, size_t, const char *, va_list);
/* fbsdkpi/drm/src/sf_drm_cdev.c and drm_gem_shmem_helper.c (m1-drm72) */
struct linux_file;
extern int (*sf_drm_mmap_pages)(struct linux_file *filp, off_t offset, size_t size, int prot,
    uint32_t *pages, unsigned int *wimg, void (**release)(void *), void **release_arg);
int drm_gem_shmem_sf_mmap_pages(struct linux_file *filp, off_t offset, size_t size, int prot,
    uint32_t *pages, unsigned int *wimg, void (**release)(void *), void **release_arg);
/* SFAgxHost.cpp: the SFAgx IOService attached to the nub */
void *sf_agx_host_of_nub(void *nub);
/* lkpi/helpers/sf_device.c, in the Rust object */
void sf_dev_printk_selftest(const struct device *dev);

#define SF_AGX_MBOX	"/arm-io/gfx-asc"

/*
 * Boot-arg sfagx_diag=1: the allocation/mapping preflight below before the probe, and in the
 * driver (agx/patches/0003) the per-mapping and per-firmware-message lines. Stage lines and
 * every failure are logged without it.
 */
int sf_agx_diag;

/*
 * SFAgx diag (temporary, boot ag02: the first GEM mapping failed with ENOMEM). The C steps
 * under the driver's first kernel mapping, one line each, on an object the size of an
 * allocator block: page allocation as the shmem backing does it, the sg table as
 * drm_prime_pages_to_sg() builds it, dma_map_sgtable(), vmap(). Nothing here touches the
 * GPU. Remove with the driver's "SFAgx: diag:" lines (agx/patches/0003).
 */
#define SF_AGX_DIAG_PAGES	600

/* layer: pd/lkpi/sf_linux_page.c and the xnu seam (patches-layer/0011) */
void sf_linux_page_stats(unsigned long *chunks, unsigned long *free_pages, unsigned long *chunk_allocs,
    unsigned long *chunk_frees, unsigned long *fail_xnu, unsigned long *fail_record);
int64_t sfos_malloc_nowait_retried(void);
void sfos_map_owned_stats(int64_t *maps, int64_t *failures);

static void
sf_agx_preflight_stats(const char *when)
{
	unsigned long chunks, free_pages, allocs, frees, fail_xnu, fail_record;

	sf_linux_page_stats(&chunks, &free_pages, &allocs, &frees, &fail_xnu, &fail_record);
	kprintf("SFAgx: diag: page pool %s: %lu chunks (%lu MiB), %lu pages free, %lu chunks taken, "
	    "%lu given back, failures: xnu %lu, record %lu; no-wait allocations that waited: %lld\n",
	    when, chunks, chunks * 64 * PAGE_SIZE >> 20, free_pages, allocs, frees, fail_xnu, fail_record,
	    (long long)sfos_malloc_nowait_retried());
}

/* the layer's memory-pressure path, called directly (xnu is not under pressure at boot) */
void sf_linux_page_pressure(void);
void sf_linux_page_pressure_stats(unsigned long *runs, unsigned long *trimmed, long long *xnu_events);

static void
sf_agx_preflight_pressure(void)
{
	unsigned long runs, trimmed;
	long long events;

	sf_linux_page_pressure();
	sf_linux_page_pressure_stats(&runs, &trimmed, &events);
	kprintf("SFAgx: diag: pressure path run by hand: %lu runs, %lu chunks returned to xnu in all; "
	    "pressure events from xnu so far: %lld\n", runs, trimmed, events);
	sf_agx_preflight_stats("after the pressure run");
}

/*
 * Thread slots: SF_AGX_DIAG_THREADS kernel threads, one after another, each enters the layer
 * (takes a slot and a task_struct) and exits. xnu reuses the thread_t addresses; the slot
 * count must not grow by the number of threads, and the layer must have noticed the reuse.
 */
#define SF_AGX_DIAG_THREADS	512
void sf_thread_slot_stats(int *used, long long *recycled);
int sf_agx_thread_churn(int count, void (*enter)(void));	/* SFAgxHost.cpp */

static void
sf_agx_diag_enter(void)
{
	linux_set_current(curthread);
	(void)current->pid;
}

static void
sf_agx_preflight_threads(void)
{
	int used0, used1, started;
	long long rec0, rec1;

	sf_thread_slot_stats(&used0, &rec0);
	started = sf_agx_thread_churn(SF_AGX_DIAG_THREADS, sf_agx_diag_enter);
	sf_thread_slot_stats(&used1, &rec1);
	kprintf("SFAgx: diag: thread slots: %d threads entered the layer and exited; slots in use "
	    "%d -> %d, slots taken over from a dead thread %lld -> %lld%s\n", started, used0, used1,
	    rec0, rec1, started == SF_AGX_DIAG_THREADS && used1 - used0 < SF_AGX_DIAG_THREADS / 4 &&
	    rec1 > rec0 ? "" : " FAILED");
}

/* 64 MiB as 64 objects of 1 MiB, the size of the driver's allocator blocks; then all freed */
#define SF_AGX_BULK_OBJECTS	64
#define SF_AGX_BULK_PAGES	((1UL << 20) / PAGE_SIZE)

static void
sf_agx_preflight_bulk(void)
{
	unsigned long total = SF_AGX_BULK_OBJECTS * SF_AGX_BULK_PAGES, got = 0, i, distinct_bad = 0;
	struct page **pages;
	u64 t0, t1, t2;

	pages = kvmalloc_array(total, sizeof(*pages), GFP_KERNEL | __GFP_ZERO);
	if (pages == NULL) {
		kprintf("SFAgx: diag: bulk FAILED: page array\n");
		return;
	}
	t0 = ktime_get_ns();
	for (got = 0; got < total; got++) {
		pages[got] = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (pages[got] == NULL)
			break;
		/* each page is its own memory: a mark, checked below */
		*(unsigned long *)page_address(pages[got]) = got + 1;
	}
	t1 = ktime_get_ns();
	for (i = 0; i < got; i++) {
		if (*(unsigned long *)page_address(pages[i]) != i + 1 ||
		    (page_to_phys(pages[i]) & (PAGE_SIZE - 1)) != 0)
			distinct_bad++;
	}
	kprintf("SFAgx: diag: bulk alloc_page: %lu of %lu pages (%lu MiB) in %llu ms, %lu with a wrong "
	    "mark or unaligned address%s\n", got, total, got * PAGE_SIZE >> 20,
	    (unsigned long long)(t1 - t0) / 1000000, distinct_bad,
	    got == total && distinct_bad == 0 ? "" : " FAILED");
	sf_agx_preflight_stats("with 64 MiB held");
	t1 = ktime_get_ns();
	for (i = 0; i < got; i++)
		__free_page(pages[i]);
	t2 = ktime_get_ns();
	kvfree(pages);
	kprintf("SFAgx: diag: bulk free: %lu pages in %llu ms\n", got, (unsigned long long)(t2 - t1) / 1000000);
	sf_agx_preflight_stats("after the bulk test");
	sf_agx_preflight_pressure();
	sf_agx_preflight_threads();
}

static void
sf_agx_preflight(struct device *dev)
{
	struct page **pages;
	struct sg_table sgt;
	struct scatterlist *sg;
	size_t max_segment;
	void *va;
	int i, n = 0, ret, wrong = 0, mapped = 0;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(40));
	max_segment = dma_max_mapping_size(dev);
	kprintf("SFAgx: diag: preflight 1/5 dma_set_mask_and_coherent(40 bits) = %d, "
	    "dma_max_mapping_size 0x%zx, PAGE_SIZE 0x%lx\n", ret, max_segment, (unsigned long)PAGE_SIZE);

	pages = kvmalloc_array(SF_AGX_DIAG_PAGES, sizeof(*pages), GFP_KERNEL | __GFP_ZERO);
	if (pages == NULL) {
		kprintf("SFAgx: diag: preflight FAILED: page array\n");
		return;
	}
	for (n = 0; n < SF_AGX_DIAG_PAGES; n++) {
		pages[n] = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (pages[n] == NULL)
			break;
	}
	kprintf("SFAgx: diag: preflight 2/5 alloc_page: %d of %d, phys first 0x%llx last 0x%llx%s\n",
	    n, SF_AGX_DIAG_PAGES, n > 0 ? (unsigned long long)page_to_phys(pages[0]) : 0ULL,
	    n > 0 ? (unsigned long long)page_to_phys(pages[n - 1]) : 0ULL,
	    n == SF_AGX_DIAG_PAGES ? "" : " FAILED");
	if (n != SF_AGX_DIAG_PAGES)
		goto out_pages;

	/* drm_prime_pages_to_sg() */
	if (max_segment == 0)
		max_segment = UINT_MAX;
	ret = sg_alloc_table_from_pages_segment(&sgt, pages, n, 0, (unsigned long)n << PAGE_SHIFT,
	    max_segment, GFP_KERNEL);
	kprintf("SFAgx: diag: preflight 3/5 sg_alloc_table_from_pages_segment(max_segment 0x%zx) = %d%s\n",
	    max_segment, ret, ret == 0 ? "" : " FAILED");
	if (ret != 0)
		goto out_pages;
	kprintf("SFAgx: diag: preflight 3/5 table: %u entries, first length 0x%x\n",
	    sgt.orig_nents, sgt.sgl->length);

	ret = dma_map_sgtable(dev, &sgt, DMA_BIDIRECTIONAL, 0);
	if (ret == 0) {
		for_each_sgtable_dma_sg(&sgt, sg, i) {
			mapped++;
			if (sg_dma_address(sg) != sg_phys(sg) || sg_dma_len(sg) != sg->length ||
			    (sg_dma_address(sg) & (PAGE_SIZE - 1)) != 0)
				wrong++;
		}
	}
	kprintf("SFAgx: diag: preflight 4/5 dma_map_sgtable = %d, nents %u: %d mapped, %d not identity "
	    "or not page aligned; first dma 0x%llx len 0x%x%s\n", ret, sgt.nents, mapped, wrong,
	    ret == 0 ? (unsigned long long)sg_dma_address(sgt.sgl) : 0ULL,
	    ret == 0 ? sg_dma_len(sgt.sgl) : 0, ret == 0 && wrong == 0 && mapped > 0 ? "" : " FAILED");
	if (ret == 0)
		dma_unmap_sgtable(dev, &sgt, DMA_BIDIRECTIONAL, 0);
	sg_free_table(&sgt);

	/*
	 * The pages in reverse order: consecutive pool pages are adjacent in kernel memory, and
	 * vmap() returns that existing address without mapping anything (boot hd01: both
	 * counters below read 0). Reversed, it has to build a mapping.
	 */
	{
		struct page **rev = kvmalloc_array(n, sizeof(*rev), GFP_KERNEL);
		int64_t maps0, fail0, maps1, fail1;

		if (rev != NULL) {
			for (i = 0; i < n; i++)
				rev[i] = pages[n - 1 - i];
			sfos_map_owned_stats(&maps0, &fail0);
			va = vmap(rev, n, VM_MAP, PAGE_KERNEL);
			sfos_map_owned_stats(&maps1, &fail1);
			if (va != NULL) {
				/* rev[n - 1] is pages[0] */
				volatile u32 *last = (volatile u32 *)((char *)va + ((size_t)(n - 1) << PAGE_SHIFT));
				u32 *direct = page_address(pages[0]);

				*last = 0x50444167;
				kprintf("SFAgx: diag: preflight 5/5 vmap %d scattered pages at %p: wrote the last "
				    "page, reads back 0x%x through the page's own address%s\n", n, va,
				    direct != NULL ? *direct : 0,
				    direct != NULL && *direct == 0x50444167 ? "" : " FAILED");
				vunmap(va);
			} else {
				kprintf("SFAgx: diag: preflight 5/5 vmap of %d pages FAILED\n", n);
			}
			kprintf("SFAgx: diag: that vmap went through the owning descriptors: %lld, was refused "
			    "by xnu (physical fallback): %lld%s\n", (long long)(maps1 - maps0),
			    (long long)(fail1 - fail0), maps1 - maps0 == 1 && fail1 == fail0 ? "" : " FAILED");
			kvfree(rev);
		}
	}

out_pages:
	for (i = 0; i < n; i++)
		__free_page(pages[i]);
	kvfree(pages);
	sf_agx_preflight_stats("after the 600-page test");
	sf_agx_preflight_bulk();
}

static int
sf_agx_pre_probe(struct platform_device *pdev)
{
	uint32_t raw = 0;
	void *host;
	int ret;

	if (pdev->dev.of_node == NULL ||
	    !of_device_is_compatible(pdev->dev.of_node, "apple,agx-t8103"))
		return (0);

	host = sf_agx_host_of_nub(pdev->sf_nub);
	if (host == NULL) {
		kprintf("SFAgx: pre-probe FAILED: no SFAgx service on the nub\n");
		return (-ENODEV);
	}
	ret = sf_rtkit_platform_init(host);
	if (ret != 0) {
		kprintf("SFAgx: pre-probe FAILED: sf_rtkit_platform_init %d\n", ret);
		return (ret);
	}
	sf_rtkit_platform_set_log_prefix("SFAgx: rtkit: ");

	ret = sf_rtkit_platform_power_state(SF_AGX_MBOX, &raw);
	kprintf("SFAgx: GFX power state 0x%08x (%s)\n", raw,
	    ret == 1 ? "powered" : ret == 0 ? "NOT powered" : "could not be read");
	if (ret != 1) {
		kprintf("SFAgx: pre-probe FAILED: GFX must be on before the driver touches SGX/ASC "
		    "(boot with sfgpudt=2, SFGpuDT powers it)\n");
		return (ret < 0 ? ret : -ENODEV);
	}

	/*
	 * The driver reports every stage with dev_info!, which ends in this helper. One line
	 * through it from C, so a boot log shows whether that path prints: expected
	 * "asahi gpu@206400000: self-test".
	 */
	kprintf("SFAgx: _dev_printk self-test, the next line should start \"asahi %s: \"\n",
	    dev_name(&pdev->dev));
	sf_dev_printk_selftest(&pdev->dev);

	if (sf_agx_diag)
		sf_agx_preflight(&pdev->dev);

	ret = sf_apple_mbox_bind(&pdev->dev, 0, SF_AGX_MBOX);
	kprintf("SFAgx: mailbox 0 of %s is %s (%d)\n", dev_name(&pdev->dev), SF_AGX_MBOX, ret);
	return (ret);
}

void
sf_agx_linux_init(void)
{
	/* LinuxKPI built with SF_LKPI_DRM prints through this (Linux %p extensions) */
	sf_driver_vsnprintf = sf_drm_vsnprintf;
	sf_agx_diag = sf_rtkit_platform_bootarg("sfagx_diag", 0);
	sf_platform_pre_probe = sf_agx_pre_probe;
	/*
	 * User mappings of GEM objects: SFDRMDri asks the DRM kext once, at mmap time, for the
	 * pages behind an offset. The driver's objects are shmem objects; m1-drm72's resolver
	 * for them pins the pages for the life of the mapping (boot ag04: without it every
	 * mmap was refused with ENODEV).
	 */
	sf_drm_mmap_pages = drm_gem_shmem_sf_mmap_pages;
}

/*
 * SF_STUB (owner: layer, sysfs): asahi_sysfs_register()/_register_idle()/_unregister() of
 * drivers/gpu/drm/asahi/sysfs.c. The attributes agx_stats, gpu_memory (and G17's
 * idle_off_delay_ms) do not exist on SiliconForest; nothing reads the snapshot or the ledger.
 * The handle is a real allocation so that unregister has something to own, as in sysfs.c.
 */
struct sf_agx_sysfs {
	const void *snapshot;
	const void *memory;
};

int
asahi_sysfs_register(struct device *dev, const void *snapshot, int export_enabled,
    const void *memory, void **handle)
{
	struct sf_agx_sysfs *s;

	if (!dev || !handle || !memory || (export_enabled && !snapshot))
		return (-ENODEV);
	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (s == NULL)
		return (-ENOMEM);
	s->snapshot = snapshot;
	s->memory = memory;
	*handle = s;
	kprintf("SFAgx: SF_STUB: no sysfs here, agx_stats and gpu_memory are not exported\n");
	return (0);
}

int
asahi_sysfs_register_idle(struct device *dev, u32 *word)
{
	return (-ENODEV);
}

void
asahi_sysfs_unregister(struct device *dev, void *handle)
{
	kfree(handle);
}

/*
 * memset_pattern16(): a libcall on Darwin targets that the compilers (clang at -Os, rustc)
 * may emit for a loop storing a 16-byte pattern. xnu does not export one to kexts.
 * optnone: must not be turned back into a call to itself.
 */
__attribute__((optnone)) void
memset_pattern16(void *b, const void *pattern16, size_t len)
{
	unsigned char *d = b;
	const unsigned char *p = pattern16;
	size_t i;

	for (i = 0; i < len; i++)
		d[i] = p[i & 15];
}

/*
 * SF_STUB: TTM is not built into this kext's DRM core (kext/CMakeLists.txt, SF_DRM_KEEP_ALL).
 * The layer's sf_drm_gem_helpers.c still carries the GEM-TTM helpers for drivers that use
 * TTM and names these three; no driver in this kext has a TTM object, so they are not
 * reached. Defined so that the collection has no unresolved symbol.
 */
struct ttm_buffer_object;
struct vm_area_struct;
struct iosys_map;
int ttm_bo_mmap_obj(struct vm_area_struct *vma, struct ttm_buffer_object *bo) { return (-ENODEV); }
int ttm_bo_vmap(struct ttm_buffer_object *bo, struct iosys_map *map) { return (-ENODEV); }
void ttm_bo_vunmap(struct ttm_buffer_object *bo, struct iosys_map *map) { }
