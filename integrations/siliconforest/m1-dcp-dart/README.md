# m1-dcp-dart: the display DARTs behind Linux's IOMMU/DMA API

For the pin's `drivers/gpu/drm/apple` (omacom/linux-aurora `6a08a08e`) on J293/T8103: the
devices behind `/arm-io/dart-dcp` and `/arm-io/dart-disp0`, served through the calls the
driver makes. Grown out of `../m1-gpu-rtkit/probe/sf_dcp_dart.cpp`, which ran on the M1
(boot dc11). **This directory is host-tested (table code) and compile evidence otherwise;
it has not run on hardware.**

| Path | What |
| --- | --- |
| `include/sf_dcp_iommu.h`, `src/sf_dcp_iommu.c` | LinuxKPI form: `iommu_*`, `dma_*`, `of_dma_configure`. Force-include the header for every file that does DMA for a display device |
| `include/sf_dart.h`, `src/sf_dart.cpp` | one stream of a firmware-provisioned T8020 DART (IOKit) |
| `include/sf_dart_table.h`, `src/sf_dart_table.c` | the page-table code, no kernel dependency |
| `test/table_test.c`, `Makefile` | host test: `make test` |
| `build-probe-kc.nix` | one-boot collection: `SFRtkitProbe` (mode 5) + `SFGpuDT` (display tree summary) |

## Streams

| Device (Linux node) | DART, stream | State found (boots dc07, dc11) |
| --- | --- | --- |
| `dcp@231c00000` (`iommus = <&dcp_dart 0 ...>`, `t8103.dtsi:663`) | `/arm-io/dart-dcp`, 0 | CONFIG 0x8001e100 (locked), TCR 0x80, root 0xbfff0c000, 4 first-level entries |
| `display-subsystem` (`<&disp0_dart 0 ...>`, `:704`) | `/arm-io/dart-disp0`, 0 | CONFIG 0x80016100 (not locked), TCR 0x80, root 0xbfff34000 plus three roots m1n1 added |
| `dcp/piodma` (`<&disp0_dart 4 ...>`, `:685`) | `/arm-io/dart-disp0`, 4 | TCR 0x80, root 0xbfff04000; TTBR 1..3 hold no valid root |

Linux sees both DARTs locked: m1n1 sets the lock on `dart-disp0` before it starts Linux
(`kboot.c:5247`), and `disp0_dart` is `status = "disabled"` in the tree until m1n1 enables it
(`t8103.dtsi:632`, `kboot.c:2440-2452`) precisely so that it is never reset. Under this lab's
hypervisor boot that lock is not set. The code here treats every stream as locked regardless:
TCR, TTBR, CONFIG and the stream enables are never written.

What Linux does with a locked T8103 root (`apple-dart.c:1053-1070`) is replace all of its
entries with Linux's own tables, into which the IOMMU core has first re-created the firmware's
mappings from the reserved-memory nodes m1n1 wrote (`kboot.c:2332-2470`: `dcp_data`,
`region57`, `region94`, `region95` and the framebuffer, each with `iommu-addresses`). Here the
firmware's entries are simply left in place (m1n1's own way, `dart.c:726-783`): nothing that
reads nonzero is ever written, so iBoot's framebuffer mapping in `dart-disp0` cannot change.
The observable difference from Linux: a mapping at an address inside a first-level slot the
firmware already populated lands in the firmware's table (in an empty leaf), not in a copy.

Addresses: allocations come from first-level slots 8..71 (iova 0x10000000..0x90000000),
skipping any slot that is not empty when the stream is opened; Linux's allocator works down
from the top of the same 0xfc000000 window. Addresses are handed out as plain stream
addresses, as Linux does; `sf_dcp_iommu_dva_mask = 1` gives m1n1's form
(`| asc-dram-mask`, 0xf00000000), which is what boot dc11 used. An address the firmware
supplies is masked to 32 bits before the lookup (`apple-dart.c:1728-1746`).

## Interface for the driver kext

Force-include `include/sf_dcp_iommu.h` (after the layer's headers are reachable; it includes
what it needs). It redirects, for bound devices only, and passes every other device through
to the layer:

- `iommu_get_domain_for_dev`, `iommu_iova_to_phys`, `iommu_map`, `iommu_unmap`,
  `iommu_map_sgtable`, `iommu_is_dma_domain`, `iommu_device_use_default_domain`,
  `iommu_device_unuse_default_domain`, `iommu_group_get`/`_put`/`_for_each_dev` (no groups:
  NULL / no-op; only the H17P paths ask), `struct iommu_domain` with `pgsize_bitmap`,
  `IOMMU_READ`/`WRITE`/`CACHE`;
- `of_dma_configure` (binds from the node's `iommus`);
- `dma_set_mask_and_coherent`, `dma_alloc_coherent`/`dma_free_coherent`,
  `dma_alloc_attrs`/`dma_free_attrs`, `dma_alloc_wc`/`dma_free_wc`, `dma_get_sgtable`(`_attrs`),
  `dma_map_resource`/`dma_unmap_resource`, `dma_mapping_error`, `dma_map_sgtable`/
  `dma_unmap_sgtable`.

`memremap` of a firmware buffer is the layer's own (`dcp.c:846`); nothing is needed from here.

Binding is automatic for a device whose `of_node` has `iommus = <&dart sid ...>` and whose
DART node has `reg` (the nodes `../m1-gpu-dt` publishes with `sfdispdt=2` do), or explicit:
`sf_dcp_iommu_bind(dev, "/arm-io/dart-dcp", 0)`.

`dma_alloc_coherent` allocates single pages, maps them at consecutive addresses and returns
one kernel view (`vmap`), as iommu-dma does, so a 16 MiB framebuffer does not need 16 MiB of
contiguous memory, with one TLB invalidate per call.

User mappings: `/dev/dri` mmap asks the DRM kext for pages (`sf_drm_mmap_pages`), so
`dma_mmap_wc`/`dma_mmap_pages` are not reached; `sf_dma_buffer_pages(dev, cpu_addr, size,
pages)` gives that resolver the physical pages of a buffer from `dma_alloc_*` (see
`ANSWERS.md` for the resolver).

Not provided: IOMMU fault reporting (`sf_dcp_iommu_report` reads the error register on
request).

`ANSWERS.md` answers the SFDcp owner's questions (binding of `piodma`, the published nubs,
`of_find_compatible_node` iteration, `status`).

## Writes

Per stream: first-level entries of the live root, only in slots that read 0; leaf entries,
only where the slot reads 0; `STREAM_SELECT`/`STREAM_COMMAND` for the TLB invalidate. Never:
TCR, TTBR, CONFIG, enables, the error register, any nonzero entry.

## Test

`make test` (host, needs only `cc`): the walk with boot dc11's numbers, mapping into an
empty slot (the leaf value dc11 wrote), refusal over firmware and own entries with rollback,
non-valid-but-nonzero slots, unreachable tables, table exhaustion, ranges across slots,
search by physical range, unmap.

## One-boot check

```sh
cd integrations/siliconforest/m1-dcp-dart
$HOME/.nix-profile/bin/nix-build --extra-experimental-features nix-command --extra-experimental-features flakes build-probe-kc.nix
# boot-args: sfrtkitprobe=5 sfgpudt=1 sfgpudt_power=0 sfdispdt=1
```

`SFRtkitProbe` mode 5 (see `../m1-gpu-rtkit/README.md`): read-only look at `dart-disp0`
streams 0 and 4 with the boot framebuffer searched for in stream 0; the DCP wake of mode 4
with the pin's `dcp_rtk_shmem_setup` code on this API; then one pass over
`dma_alloc_coherent` (5 pages), `dma_get_sgtable`, `iommu_iova_to_phys`, `dma_free_coherent`,
`dma_map_resource`/`dma_unmap_resource` on the dcp stream. `dart-disp0` is not written.

## Added by the SFDcp owner: mapping another driver's pages

`sf_dma_map_phys_pages(dev, phys[], n, &dma)` / `sf_dma_unmap_phys_pages(dev, dma, n)`
(`src/sf_dcp_iommu.c`, `include/sf_dcp_iommu.h`): `n` physical pages, in order, at one
contiguous address of the device's DART stream, with one invalidate; nothing is allocated or
copied and the pages stay owned (and pinned) by whoever passed them. SFDcp uses it to scan out
a buffer exported by the GPU driver (`../m1-dcp/kext/sf_dcp_prime.c`). On the M1 (boots pr02,
km01) every page of each imported buffer translated back correctly through the tables. The
host tests in `test/` do not cover this pair.

### Checked retirement and backing quarantine (SF-AF source)

`sf_dart_unmap_checked` validates every table walk, clears leaves and waits
for the actual native invalidate result. Failed/unreachable retirement
preserves the output and poisons the stream. PIODMA and the DCP/display
streams can share backing, so any poisoned stream blocks new allocation,
mapping and IOVA reclamation across this DCP backend until reboot. Caller
producer/in-flight DMA quiescence remains a separate requirement; a host
model or TLB acknowledgement does not establish it.

Owned coherent allocations retain pages, CPU views, IOVA and a device
reference after failed cleanup or free. Physical-page imports return
`-EBUSY` with the quarantined address if failed mapping cleanup cannot
retire it; their native PRIME caller retains the exporter reference and
pin. `sf_dma_unmap_phys_pages` now returns an error and its caller must
retain backing on any failure. The current PRIME caller does so, including
errors before a GEM import object exists. Generic scatter-gather mappings
here are backed by DCP-owned allocations; arbitrary borrowed callers must
establish their own exporter pin/lifetime contract before using those APIs.

The native DCP patch tracks PIODMA ownership on older T8103 firmware as
well as later firmware, preventing a failed unmap from being hidden by a
subsequent free. Failed first-map rollback is also flushed even when zero
whole segments were published. A declared IOMMU that fails binding returns
a blocked domain instead of falling through to physical DMA. Existing
bindings cannot switch streams while mappings may be live.

`make test` now includes 512 retirement cases using the production table
code, timeout/poison retry, unreachable tables and invalid-range refusal,
under ASan/UBSan. The full DCP SDK bundle builds. These are source/SDK
checks; this newer lifetime implementation is not in installed USB04 and
no injected hardware-failure or external-display runtime is claimed.

### Mirrored display DMA domains (SF-AF source)

A device may now bind both T8103 `iommus` entries, with Linux's one-cell
SID or five-cell SID/start64/length64 arguments, including `apple,dma-range`
consumer override. All declared windows must contain the backend's fixed
0x10000000–0x90000000 allocator aperture; narrower/aliased windows are refused.
Malformed/extra/duplicate/unavailable entries are rejected; a live
domain cannot be rebound. The native backend has room for the four
internal/external display DART blocks and eight streams. It still requires
valid, powered, firmware-provisioned inherited tables; this does not power
an external DART or invent its TTBR/firmware IOVA.

Paired IOVA allocation finds one run free in both bitmaps and reserves it
in both under the same native lock. Every coherent/page/SG/resource map,
flush, undo and free follows the whole domain. Both tables and invalidates
must complete before the address is published. A peer map failure undoes
only the successful first mapping and preserves any pre-existing peer
firmware PTE. Allocation cleanup retires only successfully mapped pages.
Either stream's retirement failure retains all backing and both IOVAs.
Translations used by the PRIME check must agree in both DARTs.

`make test` checks 524,288 paired and 2,048 single-bitmap cases, production
table pairs, partial rollback around a foreign peer PTE, and map/flush/
retirement failure in either stream, in addition to the 512 retirement
cases. A separate test feeds all three real display-builder five-cell
bindings into the production aperture parser and covers malformed, narrow,
unaligned and overridden windows. Full SFDcp SDK build passes. These are
host models and SDK evidence;
no external DART/framebuffer mapping or second output has run on the M1.
External topology publication and native PHY/mux/Type-C bindings are still
required, and all declared streams must stay powered while shared buffers
exist. USB04 remains the installed collection.
