# Answers to `../m1-dcp/NEEDS-FROM-DART.md`

State: compile evidence plus host tests, except where a boot is named. Sources: the pin
(`sources/gpu-plan/linux-aurora`) and `sources/m1n1-baseline` only.

## 1. The `piodma` device

A bare `platform_device` from `of_platform_device_create()` with only `dev.of_node` set is
enough. `sf_dcp_iommu.c` binds on the first DMA or IOMMU call on a device it has not seen:
it reads `iommus = <&dart sid ...>` from `dev->of_node`, takes the DART's register base from
that node's `reg`, opens the stream and binds (`sf_domain()` -> `sf_of_dma_configure()`).
`sf_linux_dev_dma_init()` is not needed and `dma_priv` is never looked at for a bound device:
every `sf_dma_*`/`sf_iommu_*` call on it stays in this code.

The driver's own order works: `dma_set_mask_and_coherent(&piodma->dev, 42 bits)`
(`dcp.c:3175`) is the first call and binds; `of_dma_configure()` (`:3179`) then finds the
device bound and returns 0; `iommu_get_domain_for_dev()` (`:3186`) returns the domain.
An explicit `of_dma_configure()` also overrides an earlier "not behind a DART" answer, should
a device have been used before its node was published.

What it relies on from the of layer, for the `piodma` node as `../m1-gpu-dt` publishes it:
`of_parse_phandle_with_args(np, "iommus", "#iommu-cells", 0, ...)` and
`of_address_to_resource()` on the DART node. Both nodes carry the properties
(`#iommu-cells = <5>`, `reg`).

## 2. Published nubs

With `sfdispdt=2 sfgpudt_stringprops=1`, `SFGpuDT` publishes in `gIODTPlane`:

| Node | Object | Matched by |
| --- | --- | --- |
| `dcp@231c00000` (`compatible = "apple,t8103-dcp", "apple,dcp"`) | `IOPlatformDevice`, attached to the platform expert, registered | `IONameMatch` `apple,dcp` (or `apple,t8103-dcp`), as `gpu@206400000` is by `apple,agx-t8103` |
| `display-subsystem` (`"apple,display-subsystem"`) | `IOPlatformDevice`, the same | `IONameMatch` `apple,display-subsystem` |
| `dcp@231c00000/piodma`, `dcp@231c00000/panel` | plain `IORegistryEntry` children of the dcp nub in the device-tree plane | not services: nothing can match them; the driver creates the `piodma` device itself |
| `iommu@23130c000`, `iommu@231304000`, `mbox@231c08000`, `power-management@23b738000`, `clock-disp0` | plain `IORegistryEntry` under the root | not services |
| `dcp_data@…`, `region57@…`, `region94@…`, `region95@…`, `framebuffer@…` | plain entries under `reserved-memory` (the GPU's node if it is there, else a new one) | not services |
| `framebuffer@<base>` | plain entry under the existing `chosen` | not a service |

Both nubs are registered in the same `SFGpuDT::start()`, dcp first.

## 3. `of_find_compatible_node(from, NULL, compat)`

**The layer's function does not do this.** `sf_linux_of.c` ignores `from` and returns the
first match every time, so `for_each_compatible_node` and a `for_each_matching_node` built on
it never end once anything matches. For `apple,dcp` exactly one node matches (Apple's own
`/arm-io/dcp` is `iop,ascwrap-v4`), which makes the loop infinite rather than wrong.

`../m1-gpu-dt/kernel/sf_of_iter.c` provides the Linux semantics with the layer's own child
iteration: `sf_of_find_compatible_node_from(from, type, compat)` walks the tree in pre-order,
returns the first match after `from` (from the root when NULL) and NULL at the end. Until the
layer has it, compile that file into SFDcp and, in `sf_dcp_compat.h`:

```c
struct device_node *sf_of_find_compatible_node_from(struct device_node *, const char *, const char *);
#define of_find_compatible_node(from, type, compat) sf_of_find_compatible_node_from((from), (type), (compat))
```

It is built into `SFRtkitProbe` and exercised in mode 5 (`dcp of-iter:` line: every
`iop,ascwrap-v4` node once, then an end). Proposed layer change, for the owner of
`patches-layer` (replaces the body, same file):

```diff
--- a/src/Kernel/fbsdkpi/sf/lkpi/sf_linux_of.c
+++ b/src/Kernel/fbsdkpi/sf/lkpi/sf_linux_of.c
-// the first node in tree order with the compatible string, the search starts at the root
+// the next node in tree order after `from` (the first when from is NULL) with the compatible string
+static struct device_node *
+sf_of_next(struct device_node *root, struct device_node *np)
+{
+	struct device_node *next = of_get_next_child(np, NULL);
+
+	if (next != NULL)
+		return (next);
+	while (np != NULL && np != root && np->parent != NULL) {
+		next = of_get_next_child(np->parent, np);
+		if (next != NULL)
+			return (next);
+		np = np->parent;
+	}
+	return (NULL);
+}
+
 struct device_node *
 of_find_compatible_node(struct device_node *from, const char *type, const char *compat)
 {
-	return (sf_of_node_for_entry(sfos_dt_find_compatible(compat)));
+	struct device_node *root = sf_of_node_for_entry(sfos_dt_root());
+	struct device_node *np;
+
+	if (root == NULL || compat == NULL)
+		return (NULL);
+	for (np = from != NULL ? sf_of_next(root, from) : root; np != NULL; np = sf_of_next(root, np))
+		if (of_device_is_compatible(np, compat))
+			return (np);
+	return (NULL);
 }
```

Callers that pass `from == NULL` and want the first match (the RTKit port's
`sf_rtkit_of_machine_is_compatible`) behave as before.

## 4. Machine compatible

Nothing from the display tree. `of_machine_is_compatible("apple,j700")` is false here through
the RTKit port (`arm-io,j700` does not exist), which is the right answer.

## 5. Other machines' stubs

No action.

## 6. `status`

The synthesized nodes carry `status = "okay"` on `dcp@231c00000` and `iommu@231304000`
(m1n1 sets both at boot; the static tree has the DART disabled) and no `status` anywhere
else. No node, and no child, is `"disabled"`. The layer's `of_device_is_available()` does
read `status` (absent, `"okay"` or `"ok"` is available), so
`of_get_next_available_child == of_get_next_child` gives the same result on these nodes.

## User mappings of a DMA GEM buffer (`dma_mmap_wc` / `dma_mmap_pages`)

Keep the two `-ENODEV` stubs in `sf_dcp_compat.h`: `drm_gem_dma_mmap()` is not reached, since
`/dev/dri` mmap goes through `sf_drm_mmap_pages`. What that hook needs for a DMA object is the
physical pages behind `dma_obj->vaddr`, and `sf_dcp_iommu.h` now gives them:

```c
int sf_dma_buffer_pages(struct device *dev, void *cpu_addr, size_t size, uint32_t *pages);
```

One page number per page (LinuxKPI `PAGE_SHIFT` units), valid until the buffer is freed.
The resolver to assign to `sf_drm_mmap_pages`, modelled on m1-drm72's
`drm_gem_shmem_sf_mmap_pages()` (same lookup, same reference rule: the mapping holds an
object reference until SFDRMDri reports it gone); it belongs next to the DRM glue in SFDcp
because it needs the 7.2 DRM headers, which this directory does not build against:

```c
static void sf_dcp_dma_unmap(void *arg)
{
	drm_gem_object_put(arg);	/* defer to a work item if this may run in atomic context, as the shmem one does */
}

int sf_dcp_gem_dma_mmap_pages(struct linux_file *filp, off_t offset, size_t size, int prot,
    uint32_t *pages, unsigned int *wimg, void (**release)(void *), void **release_arg)
{
	struct drm_file *priv = filp->private_data;
	struct drm_device *dev = priv->minor->dev;
	struct drm_vma_offset_node *node;
	struct drm_gem_object *obj = NULL;
	struct drm_gem_dma_object *dma_obj;
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
	if (obj == NULL)
		return -EINVAL;
	dma_obj = to_drm_gem_dma_obj(obj);
	if (!drm_vma_node_is_allowed(node, priv))
		ret = -EACCES;
	else if (drm_gem_is_imported(obj) || size > obj->size || dma_obj->vaddr == NULL)
		ret = -EINVAL;
	else
		ret = sf_dma_buffer_pages(drm_dev_dma_dev(dev), dma_obj->vaddr, size, pages);
	if (ret) {
		drm_gem_object_put(obj);
		return ret;
	}
	*wimg = SF_SHMEM_WIMG_WCOMB;	/* Linux maps these write-combined (dma_mmap_wc) */
	*release = sf_dcp_dma_unmap;
	*release_arg = obj;
	return 0;
}
```

`drm_dev_dma_dev(dev)` must be the device the buffer was allocated for, i.e. the
display-subsystem device (`apple_drv.c` allocates dumb buffers on the DRM device's parent).
The snippet is not compiled here.

## Changed since the headers were published

- `sf_dma_buffer_pages()` (above).
- One TLB invalidate per `dma_alloc_coherent`, `iommu_map_sgtable` and `dma_map_sgtable`
  call instead of one per page (`sf_dart_map_deferred()` + `sf_dart_flush()` in `sf_dart.h`).
