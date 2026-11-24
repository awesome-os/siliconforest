/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * sf_dcp_compat.h - force-included in front of every file of drivers/gpu/drm/apple built for
 * SFDcp: names Linux 7.2 has and the layer (FreeBSD LinuxKPI + drm-kmod at 6.14 with
 * m1-drm72's additions) spells differently or lacks. Each entry says which.
 */
#ifndef SF_DCP_COMPAT_H
#define SF_DCP_COMPAT_H

struct drm_connector;
void sf_dcp_note_hotplug(struct drm_connector *connector);

/* Linux 7.2 renamed struct drm_atomic_state to struct drm_atomic_commit; same object */
#define drm_atomic_commit drm_atomic_state

#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/ktime.h>
#include <linux/uuid.h>
#include <linux/of.h>
#include <linux/cleanup.h>
#include <linux/interrupt.h>
#include <linux/device.h>

/* workqueues: 7.x's default-unbound and freezable system queues; nothing freezes here */
#ifndef system_dfl_wq
#define system_dfl_wq		system_unbound_wq
#endif
#ifndef system_freezable_wq
#define system_freezable_wq	system_wq
#endif
/* the layer's takes (name, flags); Linux's takes a format, flags and arguments */
#undef alloc_ordered_workqueue
#define alloc_ordered_workqueue(fmt, flags, ...)	linux_create_workqueue_common(fmt, 1)

/* no KUnit: test-visible functions are static */
#ifndef VISIBLE_IF_KUNIT
#define VISIBLE_IF_KUNIT	static
#define EXPORT_SYMBOL_IF_KUNIT(sym)
#endif

#ifndef KTIME_MIN
#define KTIME_MIN		(-KTIME_MAX - 1)
#endif

/*
 * scoped_guard(): the layer's linux/cleanup.h pastes `___label_ ## __COUNTER__`, which does not
 * expand __COUNTER__, so two scoped_guard() in one function define the same label. Fixed in
 * the layer by patches-layer/0016; repeated here until every kext is on that layer.
 */
#define __SF_DCP_CAT2(a, b)	a ## b
#define __SF_DCP_CAT(a, b)	__SF_DCP_CAT2(a, b)
#undef scoped_guard
#define scoped_guard(_n, ...)	_scoped_guard(_n, __SF_DCP_CAT(___label_, __COUNTER__), ##__VA_ARGS__)

/* linux/of.h: scope-based put of a device node */
DEFINE_FREE(device_node, struct device_node *, if (_T) of_node_put(_T))

/*
 * linux/of.h iteration the layer's of.h does not have. The functions underneath
 * (of_find_compatible_node, of_get_next_available_child, of_find_property, of_prop_next_u32)
 * are the layer's or have to be added to sf_linux_of.c (README, "needed from the layer").
 */
struct property;
struct device_node *of_find_compatible_node(struct device_node *from, const char *type, const char *compat);
struct device_node *of_get_next_available_child(const struct device_node *node, struct device_node *prev);
const __be32 *of_prop_next_u32(const struct property *prop, const __be32 *cur, u32 *pu);
#ifndef for_each_compatible_node
#define for_each_compatible_node(dn, type, compatible)				\
	for (dn = of_find_compatible_node(NULL, type, compatible); dn;		\
	     dn = of_find_compatible_node(dn, type, compatible))
#endif
#ifndef for_each_child_of_node_scoped
#define for_each_child_of_node_scoped(parent, child)				\
	for (struct device_node *child __free(device_node) =			\
	     of_get_next_child(parent, NULL);					\
	     child != NULL; child = of_get_next_child(parent, child))
#endif
#ifndef for_each_available_child_of_node_scoped
#define for_each_available_child_of_node_scoped(parent, child)			\
	for (struct device_node *child __free(device_node) =			\
	     of_get_next_available_child(parent, NULL);				\
	     child != NULL; child = of_get_next_available_child(parent, child))
#endif
#ifndef of_property_for_each_u32
#define of_property_for_each_u32(np, propname, u)				\
	for (struct { const struct property *prop; const __be32 *item; } _it =	\
		{ .prop = of_find_property(np, propname, NULL),			\
		  .item = of_prop_next_u32(_it.prop, NULL, &u) };		\
	     _it.item; _it.item = of_prop_next_u32(_it.prop, _it.item, &u))
#endif

#include <linux/completion.h>
#ifndef DECLARE_COMPLETION_ONSTACK
#define DECLARE_COMPLETION_ONSTACK(c)	struct completion c; init_completion(&c)
#endif

/* the pin's include/uapi/drm/drm_fourcc.h; drm-kmod's is older */
#include <drm/drm_fourcc.h>
#ifndef DRM_FORMAT_P230
#define DRM_FORMAT_P230		fourcc_code('P', '2', '3', '0')
#endif
#ifndef DRM_FORMAT_MOD_VENDOR_APPLE
#define DRM_FORMAT_MOD_VENDOR_APPLE	0x0c
#endif
#ifndef DRM_FORMAT_MOD_APPLE_INTERCHANGE
#define DRM_FORMAT_MOD_APPLE_INTERCHANGE	fourcc_mod_code(APPLE, 3)
#endif

/*
 * Live device-tree edits (dcp-fabric.c disables a route node on boards with a display
 * crossbar). There is no such thing here; the functions are in kext/sf_dcp_linux.c and
 * refuse, see the README for why J293 does not get there.
 */
struct of_changeset { int sf_unused; };
void of_changeset_init(struct of_changeset *ocs);
void of_changeset_destroy(struct of_changeset *ocs);
int of_changeset_apply(struct of_changeset *ocs);
int of_changeset_update_prop_string(struct of_changeset *ocs, struct device_node *np,
				    const char *prop_name, const char *str);

struct of_device_id;
struct device_node *of_find_matching_node(struct device_node *from, const struct of_device_id *matches);
#ifndef for_each_matching_node
#define for_each_matching_node(dn, matches)					\
	for (dn = of_find_matching_node(NULL, matches); dn;			\
	     dn = of_find_matching_node(dn, matches))
#endif

/* no module loader: device tables and soft dependencies are not emitted */
#undef MODULE_DEVICE_TABLE
#define MODULE_DEVICE_TABLE(type, name)
#undef MODULE_SOFTDEP
#define MODULE_SOFTDEP(x)

/* no system sleep through LinuxKPI yet (SF-H item H7): the callbacks are compiled, not wired */
#ifndef pm_sleep_ptr
#define pm_sleep_ptr(p)		(p)
#endif
#ifndef SYSTEM_SLEEP_PM_OPS
#define SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn)	\
	.suspend = suspend_fn, .resume = resume_fn,
#endif

/* 7.x names for things the layer has under an older name or as a plain barrier */
#include <linux/slab.h>
#include <linux/iosys-map.h>
#include <asm/barrier.h>
#ifndef dma_mb
#define dma_mb()		mb()
#define dma_rmb()		rmb()
#define dma_wmb()		wmb()
#endif
#ifndef ktime_sub_us
#define ktime_sub_us(kt, usval)	ktime_sub_ns((kt), (u64)(usval) * 1000)
#endif
#ifndef time_after_eq64
#define time_after_eq64(a, b)	((s64)((a) - (b)) >= 0)
#endif
#ifndef __ALIGN_KERNEL
#define __ALIGN_KERNEL(x, a)	(((x) + ((typeof(x))(a) - 1)) & ~((typeof(x))(a) - 1))
#endif
#ifndef IOSYS_MAP_INIT_VADDR
#define IOSYS_MAP_INIT_VADDR(vaddr_)	{ .vaddr = (vaddr_), .is_iomem = false }
#endif
/* 7.2 passes the format info it already looked up; 6.14 looks it up itself */
#include <drm/drm_modeset_helper.h>
#define drm_helper_mode_fill_fb_struct(dev, fb, info, mode_cmd)	\
	drm_helper_mode_fill_fb_struct(dev, fb, mode_cmd)

/*
 * disable_work()/enable_work() (6.10): cancel and refuse queueing until enabled again. The
 * layer has only the cancel half: a work item queued between disable and enable runs here.
 */
#undef disable_work_sync
#define disable_work_sync(w)		cancel_work_sync(w)
#define disable_work(w)			cancel_work_sync(w)
#define enable_work(w)			(true)
#undef disable_delayed_work_sync
#define disable_delayed_work_sync(w)	cancel_delayed_work_sync(w)
#define disable_delayed_work(w)		cancel_delayed_work(w)
#define enable_delayed_work(w)		(true)

/* no module loader: nothing to pin, and an optional symbol of another module is absent */
#undef __module_get
#define __module_get(m)			do { } while (0)
#undef symbol_get
#define symbol_get(x)			((typeof(&x))NULL)
#undef symbol_put
#define symbol_put(x)			do { } while (0)

/* declared by the layer's linux/debugfs.h, defined nowhere: there is no debugfs */
#include <linux/debugfs.h>
#undef debugfs_remove_recursive
#define debugfs_remove_recursive(d)	do { } while (0)

/* the 7.2 atomic-commit helpers (kext/sf_dcp_drm72.c); `drm_atomic_commit` is renamed above */
struct drm_device;
struct drm_crtc;
struct drm_modeset_acquire_ctx;
struct drm_atomic_commit *drm_atomic_commit_alloc(struct drm_device *dev);
int drm_atomic_commit_init(struct drm_device *dev, struct drm_atomic_commit *state);
void drm_atomic_commit_clear(struct drm_atomic_commit *state);
void drm_atomic_commit_default_clear(struct drm_atomic_commit *state);
void drm_atomic_commit_default_release(struct drm_atomic_commit *state);
struct drm_atomic_commit *drm_atomic_commit_get(struct drm_atomic_commit *state);
void drm_atomic_commit_put(struct drm_atomic_commit *state);
int drm_atomic_helper_reset_crtc(struct drm_crtc *crtc, struct drm_modeset_acquire_ctx *ctx);
struct component_match;
void drm_of_component_match_add(struct device *master, struct component_match **matchptr,
				int (*compare)(struct device *, void *), struct device_node *node);

/* small things of newer kernels, as inlines or macros */
#include <linux/string.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <drm/drm_device.h>
#ifndef strstarts
#define strstarts(str, prefix)		(strncmp((str), (prefix), strlen(prefix)) == 0)
#endif
#ifndef dev_info_ratelimited
#define dev_info_ratelimited(dev, fmt, ...)	dev_info(dev, fmt, ##__VA_ARGS__)
#endif
/* the layer's kmalloc_objs() calls size_mul() with one argument; fix belongs in the layer */
#undef kmalloc_objs
#define kmalloc_objs(_p, _n, ...)	kmalloc_array((_n), sizeof(typeof(_p)), default_gfp(__VA_ARGS__))
#ifndef kvmalloc_obj
#define kvmalloc_obj(_p, ...)		kvmalloc(sizeof(typeof(_p)), default_gfp(__VA_ARGS__))
#endif
/* ioremap() on this layer already maps non-posted (lkpi/helpers/io.c says the same) */
#ifndef ioremap_np
#define ioremap_np(addr, size)		ioremap((addr), (size))
#endif
#ifndef of_fwnode_handle
#define of_fwnode_handle(np)		(&(np)->fwnode)
#endif
static inline bool of_node_name_eq(const struct device_node *np, const char *name)
{
	return np != NULL && np->name != NULL && strcmp(np->name, name) == 0;
}
struct device_node *of_find_node_by_path(const char *path);
static inline int device_match_name(struct device *dev, const void *name)
{
	return strcmp(dev_name(dev), name) == 0;
}
static inline int device_match_of_node(struct device *dev, const void *np)
{
	return np != NULL && dev->of_node == np;
}
void devres_close_group(struct device *dev, void *id);
/* 6.16: a DRM device may do DMA through another device; not here */
static inline struct device *drm_dev_dma_dev(struct drm_device *dev)
{
	return dev->dev;
}
/*
 * SF_STUB: user mappings on SiliconForest do not go through file_operations.mmap (SFDRMDri asks
 * the kext for pages), so drm_gem_dma_mmap() is never called; these only let it compile.
 */
struct vm_area_struct;
#define vm_flags_mod(vma, set, clear)	do { } while (0)
/* drm_gem_dma_mmap(): the page list of a dma-buf descriptor's mmap (kext/sf_dcp_mmap.c) */
struct vm_area_struct;
int sf_dcp_dma_mmap_wc(struct device *dev, struct vm_area_struct *vma, void *cpu_addr, size_t size);
#define dma_mmap_wc(dev, vma, cpu, dma, size)		sf_dcp_dma_mmap_wc(dev, vma, cpu, size)
#define dma_mmap_pages(dev, vma, size, page)		(-ENODEV)
/* only iomfb_v14_7.c (firmware 14.7 boards) asks for these */
struct clk;
static inline struct clk *clk_get_optional(struct device *dev, const char *id) { return NULL; }
struct regmap;
static inline int regmap_read(struct regmap *map, unsigned int reg, unsigned int *val) { return -ENODEV; }

/* kext/sf_dcp_linux.c */
struct device_link;
struct device_link *device_link_add(struct device *consumer, struct device *supplier, u32 flags);
void device_set_wakeup_path(struct device *dev);
struct clk;
void devm_clk_put(struct device *dev, struct clk *clk);
int driver_deferred_probe_check_state(struct device *dev);
void *devres_open_group(struct device *dev, void *id, gfp_t gfp);
int devres_release_group(struct device *dev, void *id);
int devres_remove_group(struct device *dev, void *id);
struct backlight_device;
struct backlight_ops;
struct backlight_properties;
struct backlight_device *devm_backlight_device_register(struct device *dev, const char *name,
	struct device *parent, void *devdata, const struct backlight_ops *ops,
	const struct backlight_properties *props);
void devm_backlight_device_unregister(struct device *dev, struct backlight_device *bd);
struct platform_device;
struct platform_device *of_platform_device_create(struct device_node *np, const char *bus_id,
	struct device *parent);
int of_platform_device_destroy(struct device *dev, void *data);
bool of_machine_is_compatible(const char *compat);
struct device_node *of_get_compatible_child(const struct device_node *parent, const char *compatible);
int of_property_count_u8_elems(const struct device_node *np, const char *propname);
int of_property_read_u8_array(const struct device_node *np, const char *propname, u8 *out, size_t sz);
int of_property_read_variable_u32_array(const struct device_node *np, const char *propname,
	u32 *out_values, size_t sz_min, size_t sz_max);

/* device links are not modelled: flags accepted and ignored by device_link_add() */
#ifndef DL_FLAG_AUTOREMOVE_CONSUMER
#define DL_FLAG_AUTOREMOVE_CONSUMER	(1 << 1)
#define DL_FLAG_AUTOREMOVE_SUPPLIER	(1 << 4)
#endif

#ifndef IRQF_NO_AUTOEN
#define IRQF_NO_AUTOEN		0x00080000
#endif
#ifndef IRQF_TRIGGER_RISING
#define IRQF_TRIGGER_RISING	0x00000001
#define IRQF_TRIGGER_FALLING	0x00000002
#endif
#ifndef IRQF_ONESHOT
#define IRQF_ONESHOT		0x00002000
#endif

#ifndef ARCH_DMA_MINALIGN
#define ARCH_DMA_MINALIGN	128	/* arm64 */
#endif

/*
 * Logging. Every dev_err/dev_warn/dev_notice/dev_info of the driver reaches the kernel log
 * as "SFDcp: <device>: ..."; dev_dbg is shown with the boot-arg sfdcp_debug=1.
 */
void kprintf(const char *, ...);
extern int sf_dcp_debug;
#undef dev_err
#undef dev_warn
#undef dev_notice
#undef dev_info
#undef dev_dbg
#define sf_dcp_dev_log(dev, fmt, ...)	kprintf("SFDcp: %s: " fmt, dev_name(dev), ##__VA_ARGS__)
#define dev_err(dev, fmt, ...)		sf_dcp_dev_log(dev, fmt, ##__VA_ARGS__)
#define dev_warn(dev, fmt, ...)		sf_dcp_dev_log(dev, fmt, ##__VA_ARGS__)
#define dev_notice(dev, fmt, ...)	sf_dcp_dev_log(dev, fmt, ##__VA_ARGS__)
#define dev_info(dev, fmt, ...)		sf_dcp_dev_log(dev, fmt, ##__VA_ARGS__)
#define dev_dbg(dev, fmt, ...)		do { if (sf_dcp_debug) sf_dcp_dev_log(dev, fmt, ##__VA_ARGS__); } while (0)
/*
 * sfdcp=1 binds and starts the coprocessor but refuses every atomic commit; sfdcp=2 lets
 * them through (kext/sf_dcp_drm72.c). The driver's mode_config_funcs names the helper.
 */
#include <drm/drm_atomic_helper.h>
int sf_dcp_atomic_helper_commit(struct drm_device *dev, struct drm_atomic_commit *state, bool nonblock);
#define drm_atomic_helper_commit	sf_dcp_atomic_helper_commit

/*
 * dev_err_probe(): every error a probe or bind path reports through it is logged with the
 * function and line that produced it, deferrals included (Linux logs those at debug level,
 * and boot dp03 showed only "returned -517").
 */
#undef dev_err_probe
#define dev_err_probe(dev, err, fmt, ...)	({				\
	int sf_err_ = (err);							\
	kprintf("SFDcp: %s: %s:%d: error %d: " fmt, dev_name(dev), __func__, __LINE__, sf_err_, ##__VA_ARGS__); \
	sf_err_; })

/*
 * dmam_alloc_coherent()/dmam_free_coherent(): m1-dcp-dart's header redirects the unmanaged
 * names to the DART but not these, so the AFK ring buffers (afk.c:262) went to LinuxKPI's
 * generic allocator with a 32-bit mask and failed (boot dp05: "Failed to allocate 32768
 * bytes buffer"; no RAM below 4 GiB on this machine). kext/sf_dcp_linux.c: the DART
 * backend's allocation, freed by a devres action.
 */
#include <linux/dma-mapping.h>
void *sf_dcp_dmam_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle, gfp_t gfp);
void sf_dcp_dmam_free_coherent(struct device *dev, size_t size, void *vaddr, dma_addr_t dma_handle);
#undef dmam_alloc_coherent
#define dmam_alloc_coherent(dev, size, handle, gfp)	sf_dcp_dmam_alloc_coherent((dev), (size), (handle), (gfp))
#undef dmam_free_coherent
#define dmam_free_coherent(dev, size, vaddr, handle)	sf_dcp_dmam_free_coherent((dev), (size), (vaddr), (handle))

/*
 * drm_driver.gem_prime_import: buffers exported by another DRM kext are imported by
 * kext/sf_dcp_prime.c. The driver names its GEM ops through this macro (apple_drv.c:186).
 */
#include <drm/drm_gem_dma_helper.h>
struct dma_buf;
struct drm_gem_object *sf_dcp_prime_import(struct drm_device *dev, struct dma_buf *dma_buf);
#undef DRM_GEM_DMA_DRIVER_OPS_WITH_DUMB_CREATE
#define DRM_GEM_DMA_DRIVER_OPS_WITH_DUMB_CREATE(dumb_create_func)		\
	.dumb_create		   = (dumb_create_func),			\
	.gem_prime_import_sg_table = drm_gem_dma_prime_import_sg_table,		\
	.gem_prime_import	   = sf_dcp_prime_import

/* patches/0002: the IOMFB callback dispatcher reports each callback (kext/sf_dcp_linux.c) */
void sf_dcp_note_callback(struct device *dev, int tag, const char *fourcc, u32 in_len, u32 out_len,
			  unsigned int context, bool handled);

#endif
