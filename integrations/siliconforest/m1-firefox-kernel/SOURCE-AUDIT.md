# Frozen GPU BO import audit, 2026-11-15

Source contracts below are read-only workstation evidence. No M1 probe, UPL,
cache operation or caller-death test ran during the owner's machine pause.

| Installed input | Exact source |
|---|---|
| CIO29 kernel9scpbhl8… | `/nix/store/wzqlii8czf0n64p01i3k9sv9agjhdl3m-source/src/Kernel/xnu` |
| SFDRMDri h7w1ph04… | `/nix/store/gj14r67462ndvmf7f2al6ah8isjhk4wc-puredarwin-dcp-tree` |
| SFAgx0agqznr4… | `/nix/store/0n94n35s2kylg2c787f94p4mm417mqhs-puredarwin-agx-tree` |
| AVD import SDKdxhmqiiv… | `/nix/store/y6pr54piv1wc2w2nkqcris680jy8yq02-puredarwin-apple-avd-tree` |

These are derivation-resolved installed inputs, not an assumption that the
mutable working tree is the installed kernel. SFAgx includes a prelinked Rust
asahi.o; this audit does not claim a fresh source review of that entire object.
No AGX/DRM code changes are included in the combined candidate.

## Original BO and page provenance

1. XNU `bsd/kern/kern_mman.c:sf_cdev_mmap_entry` asks the actual driver for its
   page array/cache policy. It creates a device pager with NOPHYSCACHE, not the
   physically-contiguous flag, populates each supplied PPN, and registers the
   driver's release callback before publishing the named entry. Cached BOs use
   VM_WIMG_USE_DEFAULT. No physical-contiguity shortcut is permitted here.
2. Frozen DRM `src/Kernel/fbsdkpi/drm/src/sf_lfile_lkpi.c` dma-buf mmap holds
   get_file plus the original VMA/GEM page reference until the pager's release
   work runs vm_ops.close and fput. The shmem exporter supplies actual PPNs and
   its cache policy (the lab's0009-drm-gem-shmem-dma-buf-mmap-pages.patch).
3. XNU `IOGeneralMemoryDescriptor::wireVirtual` uses InOut/read+write and
   UPL_SET_IO_WIRE|UPL_SET_LITE. `vm_map_create_upl` and
   `vm_object_iopl_request` retain the VMO/activity and record each real private
   alias PPN. `vm_page_make_private` has already substituted the real PPN;
   private pages are not fictitious sentinel pages. This branch is not the
   physically-contiguous single-device-page branch.
4. A UPL/activity hold by itself is insufficient evidence that the original
   device pager's GEM callback cannot run. `device_vm.c:device_pager_map` retains
   on first map; last_unmap drops that reference and deallocate invokes the BO
   release callback before destroying the memory object. The extra mapping
   reference is therefore essential, not merely a convenient CPU pointer.
5. `IOMemoryDescriptor.cpp:memoryReferenceCreate` virtual branch uses the
   owner's original map/range in mach_make_memory_entry_internal. InOut grants
   writable access. No kIOMemoryReferenceCOW is requested. `memoryReferenceMap`
   maps that named entry with **copy=false** (around1231). Retained kernelMap
   therefore maps the same original pager/VM object with the existing cache
   policy; it does not create a raw-PPN replacement pager. It remains mapped
   after the caller unmaps or its task terminates, retaining the original BO.
6. AVD owns the MD, prepared UPL, kernelMap and retained owner task. gLock
   serializes decode/destruction/reaping; synchronous decode exits only after
   completion or failure. On failure the candidate poisons before any later
   destroy; dma_free then keeps every pin/map/IOVA until reboot. Successful
   unmap/invalidation precedes complete/release. A surviving GPU consumer owns
   its own original GEM/fileport/mapping references and completion leases.

Native admission may still refuse exported GPU BO UPLs. The future window must
exercise real exported BOs, independent old/new pixel patterns, caller unmap/
death, held frames and active GPU readers. Source inspection is not that test.

## Actual available cache operation

Exact kernel Mach-O symbols and instructions were inspected locally.
`flush_dcache` is DSB_SY/RET; flush_dcache64 branches there, and
`dcache_incoherent_io_flush64` is a barrier/result store/return.
IOMemoryDescriptor::performOperation does not supply missing CMO on this build.

`osfmk/arm/caches_internal.h` declares
`CleanPoC_DcacheRegion_Force(vm_offset_t,size_t)`. It is a defined external
N_SECT symbol in the exact frozen kernel. `arm64/caches_asm.s` explicitly says
Apple DC CVAC is a NOP. Force disables preemption, enables DC MVA through HID4/
EHID4, calls the real DC CIVAC range loop and restores controls/preemption.
This existing virtual-range operation is used on the retained shared DATA
mapping; a new physical-range KPI or cache-incompatible alias is unnecessary.

The final collection verifier follows AVD's actual BL to the relaxed literal
import stub, resolves its DYLD_CHAINED_PTR_64_KERNEL_CACHE pointer (format8,
cacheLevel0) and requires the destination to be the kernel Force function.
The pointer layout is in the local dyld `include/mach-o/fixup-chains.h`.
The original Force prologue bytes are also checked. This establishes linking,
not runtime cache correctness or performance.

The ownership sequence and cooperative CPU/GPU exclusion are explicit in the
patch and README. Post-device CIVAC is valid only after pre-device cleaning
and no intervening CPU dirt; otherwise it can overwrite the decoded result.
No barrier, successful compilation, prior private-buffer success or old
CPU-copy→GPU success substitutes for the new AVD→AGX hardware visibility test.
No new GPU fence ABI is proposed; existing GPU-completion leases must be proved
by the media/Firefox clients before recycling any capture.
