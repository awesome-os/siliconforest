# What SFDcp needs from m1-dcp-dart and the display device tree (m1-gpu-dt)

Written by the SFDcp owner for relay. Each item says what is stubbed locally meanwhile
(`kext/sf_dcp_linux.c`) so the link is not held up.

## Used as delivered (no change asked)

- `m1-dcp-dart/include/sf_dcp_iommu.h` force-included for every driver file and for the pin's
  `drm_gem_dma_helper.c`, `drm_fb_dma_helper.c`; `src/sf_dcp_iommu.c`, `src/sf_dart_table.c`,
  `src/sf_dart.cpp` built into the kext. `struct iommu_domain.pgsize_bitmap`, `IOMMU_READ/WRITE`
  and all `iommu_*`/`dma_*` names the driver calls resolved with it.
- Binding from the node's `iommus` property is relied on for `dcp`, `piodma` and the
  display-subsystem device. SFDcp calls `sf_dcp_iommu_bind()` nowhere.

## Questions / requests

1. **`piodma` device.** `dcp.c:3170` does `of_platform_device_create(node, NULL, dcp->dev)` for
   the `piodma` child and then DMA-maps through that device. Locally this creates a bare
   `platform_device` with `dev.of_node` set to the child and no `dma_priv`. Does
   `sf_dcp_iommu.c` bind such a device from its node on first use, or does it need
   `sf_linux_dev_dma_init()` (the layer's default DMA tags) to have run on it first?
2. **Node names the IOKit personality matches.** `SFDcp` matches `IONameMatch`
   `apple,dcp` and `apple,display-subsystem` on `IOPlatformDevice`. Confirm that the published
   nubs are `IOPlatformDevice`s matched by compatible in that way (as `gpu@206400000` is by
   `apple,agx-t8103`), and that `piodma` is NOT published as its own matchable nub (the driver
   creates it).
3. **`of_find_compatible_node(from, NULL, compat)` over the published nodes.** `apple_drv.c`
   walks the tree for `apple,dcp` nodes (`for_each_matching_node`) to build the component
   match; locally `of_find_matching_node` is a loop over that function. It must find the
   published `dcp` node starting from NULL and return NULL after it.
4. **Machine compatible.** `of_machine_is_compatible("apple,j700")` etc. are answered by the
   RTKit port's `sf_rtkit_of_machine_is_compatible`. Nothing asked unless the display tree
   wants to answer it itself.
5. **Not provided by the layer, stubbed, only reached on other machines:**
   `of_find_node_by_path` (`iomfb_v26_6.c:1035`, returns NULL),
   `of_translate_dma_region_checked` (`iomfb_v14_7.c:1849`, returns -ENODEV),
   `of_changeset_*` (`dcp-fabric.c:3614`, refuse). No action unless those machines matter.
6. **`status` properties.** `of_get_next_available_child` is `of_get_next_child` here (the
   layer does not read `status`). If the synthesized display nodes carry `status =
   "disabled"` children, say so.

## Added after boot dp05

7. **`dmam_alloc_coherent` / `dmam_free_coherent` are not redirected by `sf_dcp_iommu.h`.**
   `afk.c:262` allocates each AFK ring buffer with the managed variant, which went to
   LinuxKPI's generic allocator (32-bit mask, contiguous below 4 GiB: impossible here) and
   failed with "Failed to allocate 32768 bytes buffer". SFDcp now redirects both names itself
   (`include/sf_dcp_compat.h`, `kext/sf_dcp_linux.c`: `sf_dma_alloc_coherent` plus a devres
   action). If the header takes them over, drop SFDcp's copy.
