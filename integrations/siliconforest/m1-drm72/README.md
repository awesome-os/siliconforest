# m1-drm72: the DRM C core at the Linux 7.2.11 API level (SF-F item F9)

SiliconForest's DRM kexts compile `freebsd/drm-kmod` rev `c16de0f9` (Linux 6.14 level) on FreeBSD
LinuxKPI. The Rust wrappers the Asahi GPU driver is written against are Linux 7.2.11
(`sources/gpu-plan/linux-aurora`, Omacom's tree: mainline 7.2.11 plus Asahi and Omacom
changes). This directory is the patch series that brings the parts of the C core those
wrappers call to that level, the port of the GEM shmem helper, and the recipes that prove it
compiles, links, and matches the unmodified wrappers.

Everything here is compile and link evidence. Nothing has run.

## Rebuild

```
export PATH=$HOME/.nix-profile/bin:$PATH
F="--extra-experimental-features nix-command --extra-experimental-features flakes"
cd integrations/siliconforest/m1-drm72
nix-build $F build-kext.nix --no-out-link              # SFDRMmsm + SFDRMDri, shmem helper in
nix-build $F default.nix -A kextDefault --no-out-link  # the same without the shmem helper
nix-build $F default.nix -A kextOnLayer --no-out-link  # on m1-rust-kext's patched layer (16 KiB)
nix-build $F default.nix -A bindingsGen --no-out-link  # bindgen over the patched headers
nix-build $F default.nix -A matrix --no-out-link       # the 7.2 wrappers; wrappers.md in the output
nix-build $F x86-breakage.nix --no-out-link            # what breaks in amdgpu/nouveau/i915/radeon
```

- `default.nix` applies `patches/drm-kmod/*.patch` to the flake's `drm-kmod-src` (which already
  has SiliconForest's removals and `drm-kmod-sf.patch`) and `patches/siliconforest/*.patch` to the
  arm64 DRM-kext tree slice, with `patch -F0`, and builds the flake's own `drm-msm-arm64`
  recipe on the result.
- A kext is linked with its undefined symbols left for the kernel collection, so linking
  proves little. The kext recipes therefore compare `nm -u` of the result with the 6.14
  build of the same kext on the same layer and fail on any added symbol, and check that the
  new entry points are defined in the binary. `$out/nix-support/` has the lists and the
  compile commands.
- `rust.nix` imports `../m1-rust-kext/lkpi` (bindgen step, C flags, Rust toolchain, module
  matrix) and re-instantiates its derivations with the patched source paths substituted.
  Nothing is copied. It follows that directory as it changes; four things in it are
  transitional for this ticket and are undone here, see "For m1-rust-kext" below.

## The series

`patches/drm-kmod/` (against drm-kmod as SiliconForest patches it):

| patch | what | how |
|---|---|---|
| 0001 dma-fence | `dma_fence_init64`, inline lock (`extern_lock`/`inline_lock`, `dma_fence_spinlock`, `dma_fence_lock_irqsave`…), `INITIALIZED`/`INLINE_LOCK`/`SEQNO64` flag bits, `dma_fence_check_and_signal*`, `dma_fence_driver_name`/`timeline_name`, `dma_fence_was_initialized`, `dma_fence_test_signaled_flag`, `__dma_fence_is_later(fence, …)`; `dma_fence_ops` loses `use_64bit_seqno`, `fence_value_str`, `timeline_value_str` | adapted: additions to FreeBSD's BSD-licensed implementation (Linux's `dma-fence.c` is GPL-2.0-only and is not taken) |
| 0002 drm_exec | `drm_exec.[ch]` | replaced wholesale with 7.2, no adaptation needed |
| 0003 drm_gem | `drm_gem_object.gpuva.lock` (init/destroy), `drm_gem_gpuva_assert_lock_held(gpuvm, obj)`, `drm_gem_is_imported()`, GPUVA list always initialised | hand edit of the 6.14 files |
| 0004 drm_gpuvm | `drm_gpuvm.[ch]` | replaced wholesale; one added include |
| 0005 scheduler | `sched_main.c`, `sched_entity.c`, `sched_fence.c`, new `sched_rq.c` and `sched_internal.h`, trace header, `gpu_scheduler.h` | replaced wholesale, then drm-kmod's FreeBSD adaptations re-applied (see below) |
| 0006 drm_drv | `__drm_dev_alloc()`; `drm_dev_alloc()` built on it as in 7.2 | hand edit |
| 0007 drm_syncobj | `drm_syncobj_set_wait_hint()` and the two `drm_file` fields | hand edit; the hint is stored, not acted on |
| 0008 GEM shmem helper | `drm_gem_shmem_helper.[ch]`, new | 7.2 file, ported (next section) |
| 0009 shmem: pages for a dma-buf descriptor mmap | `drm_gem_shmem_helper.c` | SiliconForest: `drm_gem_shmem_mmap()` answers the page question of layer patch 0023 (`struct sf_vma_pages`) |

`patches/siliconforest/` (against the SiliconForest tree):

| patch | what |
|---|---|
| 0001 | `fbsdkpi/drm/include/linux/sf_lkpi72.h`: the Linux kernel API the 7.2 files call that LinuxKPI lacks, each with its semantics stated |
| 0002 | `fbsdkpi/drm/drm.cmake`: `scheduler/sched_rq.c` in the core source list; option `SF_DRM_GEM_SHMEM` (off) adds `drm_gem_shmem_helper.c` |

Against the Omacom 7.2.11 files the replaced and ported files differ by added lines only
(`#ifdef __FreeBSD__` branches, `#ifdef __linux__` around what cannot build); no Linux line
is removed. SPDX and copyright lines are as in the sources.

FreeBSD adaptations in the replaced files, all in drm-kmod's idiom:
- `sched_entity.c`: `current_exiting()`/`fatal_signal_pending()` instead of `PF_EXITING` and
  `exit_code`; drm-kmod's `entity->rq->sched == NULL` guard carried forward.
- `gpu_scheduler_trace.h`: the five 7.2 tracepoints as KTR records, the two `*_enabled()` tests false.
- `gpu_scheduler.h`: `LINUXKPI_DEFINE_CLASS` (FreeBSD's `DEFINE_CLASS` is kobj's);
  `drm_sched_for_each_pending_job()` written out, because LinuxKPI's `scoped_guard()` does
  not take classes.
- `sf_lkpi72.h` included where needed.

### Mainline 7.2 or Omacom

Taken from linux-aurora, so the Omacom additions are in. Compared with `gregkh/linux v7.2.11`:

- **Mainline 7.2:** `drm_sched_init_args`, `drm_client_id`, `RESET`/`NO_HANG`, `cancel_job`,
  `sched_rq.c`, `drm_sched_for_each_pending_job`; `drm_gpuvm_map_req`,
  `DRM_GPUVM_IMMEDIATE_MODE`, `drm_gpuva_unlink_defer`, `drm_gpuvm_bo_deferred_cleanup`,
  `drm_gpuvm_bo_put_deferred`, `gpuva.lock`; `__drm_dev_alloc`; all of the dma-fence
  changes; `drm_gem_shmem_init`/`release`, `__drm_gem_shmem_free_sgt_locked`.
- **Omacom/Asahi, not mainline:** `DRM_GPUVA_REPEAT`, `drm_gpuva_op_map.flags` and
  `gem.range`, `drm_gpuvm_ops.sm_can_merge_flags`, `drm_gpuvm_bo_unmap`;
  `drm_sched_backend_ops.owner` and `.retain_job_on_enodev`, `drm_sched_fence.sched_name`,
  `disable_work_sync()` in `drm_sched_fini()`; `drm_syncobj_set_wait_hint` and its `drm_file`
  fields; removal of `DRIVER_GEM_GPUVA`; in the shmem helper `cpu_vma_count`,
  `reclaimable_cpu_mappings`, `pages_no_reclaim`, `cpu_pages`,
  `drm_gem_shmem_idle_pages_releasable_locked`, `drm_gem_shmem_release_idle_pages_locked` and
  the stricter `drm_gem_shmem_is_purgeable`.

### What the Asahi driver (common code and G13) uses

- **Scheduler:** `Scheduler::new`, `Entity::new`, `Job` (init, arm, push, dependencies,
  cancel), `Status::NoDevice`, kernel priority. The wrapper exposes neither
  `drm_sched_for_each_pending_job` nor stop/start/resubmit, so the driver cannot use them.
- **GPUVM:** immediate mode on every VM, `sm_map`/`sm_unmap`/`bo_unmap`, `REPEAT` mappings,
  deferred unlink and put, `GpuVmExec`.
- **GEM shmem:** object create/release, `sg_table` (pinned, DMA-mapped), `vmap`, madvise and
  purge, idle-page release, `pages_no_reclaim`, `reclaimable_cpu_mappings`. `gem.rs` also reads `filp->f_mapping` flags
  (`AS_UNEVICTABLE`, `AS_INACCESSIBLE`) directly; there is no address_space on this layer, so
  that function needs a SiliconForest branch when the driver is built (F13).
- **syncobj, dma-fence:** handle lookup, `fence_get`, `replace_fence`, `add_point`, the wait
  hint; `FenceChain`; own fences through `dma_fence_init64`.
- **Not needed and not ported:** `drm_gem_shmem_prime_import_no_map`, the Linux fault
  handler, huge-page mappings, KUnit-only entry points.

## The GEM shmem helper port

`drm_gem_shmem_helper.c` is Linux's file with `__FreeBSD__` branches; the notes at its top
say the same as this section.

- **Backing.** `drm_gem_object_init()` gives the object a LinuxKPI shmem file, which here is
  `sf_drm_shmem.c`: wired kernel pages, one LinuxKPI page each, allocated zeroed on first
  read. drm-kmod does not build `drm_gem_get_pages()`/`drm_gem_put_pages()`, so the helper
  fetches pages with `shmem_read_mapping_page_gfp()` and returns them with `put_page()`.
  There is no swap, LRU, GFP mask or dirty tracking behind it: `pages_no_reclaim`, the
  unevictable flag and the dirty/accessed marks do nothing. Purging frees the pages
  (`shmem_truncate_range`); short of freeing the object, nothing else returns memory.
- **CPU mappings.** `/dev/dri` mmap does not go through `file_operations.mmap` or a fault
  handler. SFDRMDri asks the kext once for the physical pages of an offset
  (`sf_dri_ops.mmap_pages`). The port adds that resolver, `drm_gem_shmem_sf_mmap_pages()`,
  for a driver to assign to `sf_drm_mmap_pages`. A mapping holds `pages_use_count` and an
  object reference until SFDRMDri reports the last mapping gone. Mappings cannot be revoked,
  so `reclaimable_cpu_mappings` has no effect: a mapped object is neither purgeable nor
  idle-releasable. `drm_gem_shmem_mmap()` and `drm_gem_shmem_vm_ops` exist (the wrapper
  names them) but are not reached from `/dev/dri`; `.fault` refuses.
- **Left out on this platform:** `drm_gem_shmem_prime_import_no_map` (needs the 7.2
  `drm_prime.c`), mmap of imported dma-bufs (SiliconForest's dma-buf has none), huge mappings.
  `drm_gem_shmem_dumb_create` sizes the buffer the 6.14 way (`drm_mode_size_dumb` is newer).
- **Page size.** The code uses `PAGE_SIZE`/`PAGE_SHIFT` only and builds on both the pinned
  4 KiB layer and m1-rust-kext's 16 KiB layer (`kextOnLayer`). Things that depend on the
  page size being right rather than on this code:
  - the page numbers handed to SFDRMDri are `phys >> PAGE_SHIFT` in LinuxKPI pages, as in
    the tree's other resolvers; they mean what xnu expects only when LinuxKPI's page size
    is the kernel's (16 KiB on the M1);
  - `linux_shmem_read_mapping_page_gfp()` takes an `int` page index; the helper refuses
    objects of more than `INT_MAX` pages;
  - `drm_exec.c` grows its object array by `PAGE_SIZE / sizeof(void *)` entries (larger
    steps at 16 KiB, harmless).
- **Licence.** The helper is GPL-2.0, unlike the rest of the DRM core PureDarwin keeps
  (it removes GPL-only files, see `drm-kmod-remove.txt`). It is therefore behind
  `SF_DRM_GEM_SHMEM`, off by default, for a kext that is GPL-2.0 compatible as a whole.
  `build-kext.nix` switches it on in SFDRMmsm only to prove it compiles and links.

## LinuxKPI gaps and what stands in for them

| 7.2 code needs | here | safe? |
|---|---|---|
| `disable_work_sync()` (Omacom, `drm_sched_fini`) | `cancel_work_sync()` | Same as mainline 7.2, which has no such call. The race Omacom closes (work queued between the `pause_submit` check and teardown) stays open. |
| `__module_get()` | no-op, like LinuxKPI's `module_get()` | yes while kexts in the collection are not unloaded |
| `us_to_ktime`, `init_llist_node`, `llist_on_list`, `refcount_dec_not_one` | exact implementations | yes (LinuxKPI refcounts do not saturate) |
| `DEFINE_CLASS`, class-based `scoped_guard` | `LINUXKPI_DEFINE_CLASS`, a written-out loop | compile-tested only; no caller in the tree |
| tracepoints | KTR records | yes |
| `PF_EXITING`, `exit_code == SIGKILL` | `current_exiting()`, `fatal_signal_pending()` | drm-kmod's own substitution |
| `sched_util_min_scope_enter()` (Omacom) | absent; hint ignored | yes, performance only |
| dma-fence ops detach under RCU after signalling | not done: `fence->ops` stays set | Conservative for the 6.14-level code that dereferences it. A fence's issuer must outlive its fences, as in 6.14. |
| `dma_fence_signal*()` returning void | still return int | yes |
| inline fence lock | `struct mtx` in the fence, destroyed in `dma_fence_release()` before the release callback | A release callback must not take the fence's own inline lock; none in the tree does. |
| page cache, folios, GFP mask, unevictable LRU | see shmem section | functional difference, not a crash risk by itself |
| fault handler, `vmf_insert_pfn_mkwrite`, PTE zap | SFDRMDri resolver, eager and pinned | mapped objects are never reclaimed |
| `drm_mode_size_dumb`, `drm_gem_is_prime_exported_dma_buf` | 6.14 sizing; function left out | yes |

## What stays at 6.14 and why

- `drm_gem.c`, `drm_file.c`, `drm_drv.c`, `drm_syncobj.c`, `drm_prime.c` apart from the
  additions above. The wrappers need nothing else from them, and 7.2's versions depend on
  what LinuxKPI lacks (syncobj handles in an xarray with `xa_alloc`/marks, GEM LRU and huge
  tmpfs changes, the mm-subpage mmap changes of the Omacom tree).
- `enum drm_driver_feature` keeps `DRIVER_GEM_GPUVA` (Omacom removed it; nouveau names it).
- `dma-fence-array.c`, `dma-fence-chain.c`, `dma-resv.c`: FreeBSD's implementations, with
  their own lock members. `dma_fence_chain` is opaque to the wrappers.
- `struct dma_fence.lock` stays as an alias of `extern_lock`.
- TTM, KMS, display helpers, every driver.
- `LINUXKPI_VERSION` stays 61400: it selects LinuxKPI behaviour, and the 7.2 files compile
  against that selection.

## In-tree drivers

- **arm64 (`SFDRMmsm`, `SFDRMDri`):** build unchanged; no call site needed adapting (the
  clean-room msm driver uses neither the scheduler nor GPUVM). Same undefined-symbol set as
  the 6.14 build.
- **x86** (`x86-breakage.nix` builds each kext from the flake's recipe on the patched
  sources with `ninja -k 0` and collects the compiler's errors; nothing is fixed):

  | kext | result | call sites |
  |---|---|---|
  | SFDRMi915 | builds | none |
  | SFDRMradeon | builds | none |
  | SFDRMamdgpu | 4 of 955 objects fail | `amdgpu_cs.c:967,1218,1335` `drm_exec_for_each_locked_object(exec, index, obj)` (7.2: no index argument); `amdgpu_device.c:2870` `drm_sched_init()` with 11 arguments (7.2: `struct drm_sched_init_args *`); `amdgpu_job.c:184` `DRM_GPU_SCHED_STAT_NOMINAL` (7.2: `RESET`); `amdgpu_job.c:207` `drm_sched_job_init()` without `drm_client_id`; `amdgpu_vm_tlb_fence.c:74` `.use_64bit_seqno` (7.2: `dma_fence_init64()`) |
  | SFDRMnouveau | 3 of 910 objects fail | `nouveau_exec.c:188`, `nouveau_sched.c:373` `DRM_GPU_SCHED_STAT_NOMINAL`; `nouveau_sched.c:90` `drm_sched_job_init()`; `nouveau_sched.c:423` `drm_sched_init()`; `nouveau_uvmm.c:1210` `drm_gpuvm_bo_obtain()` (gone in 7.2); `nouveau_uvmm.c:1305` `drm_gpuvm_sm_map()` with positional arguments (7.2: `struct drm_gpuvm_map_req *`) |

  These are the places the compiler rejects. amdgpu and nouveau were written for the 6.14
  scheduler; whether they behave on the 7.2 one after these sites are adapted (job
  cancellation on teardown, the run-queue rework) is not something a build shows. A kext
  that must stay at 6.14 can keep building from the unpatched drm-kmod, since every kext
  compiles its own copy of the core.

## Rust wrappers

`nix-build default.nix -A matrix` compiles the kernel crate once per wrapper with that
module switched on, on `../m1-rust-kext/lkpi`'s core tier, and writes `wrappers.md`. The
wrapper sources are Linux's files unchanged (the recipe compares them). Result on 2026-11-10:

| wrapper | crate builds | errors in the wrapper's files | errors elsewhere (module: count) |
|---|---|---|---|
| dma_fence | yes | 0 | none |
| drm::device | no | 0 | acpi: 4, device: 26, devres: 14, driver: 3, drm::file: 1 |
| drm::driver | no | 0 | device: 26, devres: 14, drm::file: 1 |
| drm::file | no | 1: `drm/file.rs:167` mismatched types | acpi: 4, device: 26, devres: 14, driver: 3 |
| drm::ioctl | no | 0 | acpi: 4, device: 26, devres: 14, driver: 3, drm::file: 1 |
| drm::gem | no | 0 | acpi: 4, device: 26, devres: 14, driver: 3, drm::file: 1 |
| drm::mm | no | 0 | acpi: 4, device: 26, devres: 14, driver: 3, drm::file: 1 |
| drm::sched | no | 0 | acpi: 4, device: 26, devres: 14, driver: 3, drm::file: 1 |
| drm::syncobj | no | 0 | acpi: 4, device: 26, devres: 14, driver: 3, drm::file: 1 |
| drm::gpuvm | no | 0 | acpi: 4, device: 26, devres: 14, driver: 3, drm::file: 1 |
| drm::gem::shmem | no | 0 | acpi: 4, cred: 6, debugfs: 8, device: 26, devres: 14, dma: 2, driver: 3, drm::file: 1, fs: 16, scatterlist: 6, seq_file: 1, sync: 1, task: 9, uaccess: 4 |

How to read it:
- Only `dma_fence` compiles to an rlib. Every DRM wrapper depends on `kernel::device`, and
  that module (with `devres`, `driver`, `acpi`) does not compile on LinuxKPI; the plan
  replaces those modules (F8). `drm::gem::shmem` additionally pulls `scatterlist`, `fs`,
  `cred`, `debugfs`, `uaccess`, `dma`. None of these errors is in DRM code or DRM C API.
- rustc resolves names and type-checks all bodies before giving up, so a wrapper with 0
  errors in its own files has every C function, type, constant and field it names present
  with the type it expects. Against the 6.14 core the same measurement
  (`docs/roadmap/research/rust-kernel-crate-on-lkpi.md`) had 17 errors in `drm::sched`'s
  own files, 27 in `drm::gpuvm`, 32 in `drm::gem::shmem`, 4 in `drm::gem`, 2 in
  `dma_fence`: wrong argument counts, missing fields, missing functions. Borrow checking
  and code generation have not run for the DRM wrappers.
- `drm/file.rs:167` is `c"system".as_ptr()`: `*const i8` on an Apple target where the
  kernel crate's `c_char` is `u8`. It is not C API; `../m1-rust-kext/lkpi/patches/0004`
  fixes it with a one-line edit, which this measurement leaves out to keep the wrappers
  unmodified.

## For m1-rust-kext

`rust.nix` undoes or adds these so that the wrappers are measured unmodified; each belongs in
`../m1-rust-kext/lkpi` once this series is the base:

1. `helpers/drm.c` (a copy of Linux's without the shmem helpers): delete, and add
   `CONFIG_DRM_GEM_SHMEM_HELPER` to `cflags.nix` `helperDefines`. Linux's file then compiles.
2. `helpers/sf_dma_fence.c` (`dma_fence_init64` as `dma_fence_init`): delete. With this
   series it would shadow the real function and drop the 64-bit flag.
3. The `kernel/dma_fence.rs` hunk of `patches/0005` (three removed `dma_fence_ops` fields):
   delete.
4. Add `rust_helper_atomic_sub` (to `helpers/atomic.c`) and `rust_helper_noop_llseek`
   (`rust-helpers/drm72.c` here has both).
5. Point `drmkmod` and `tree` at the patched sources (`(import ../m1-drm72).drmkmod`,
   `.layerTree`).

## Into the SiliconForest tree

- `patches/siliconforest/0001` and `0002` as they are.
- `patches/drm-kmod/0001`–`0007` folded into `fbsdkpi/drm/drm-kmod-sf.patch` (or applied
  after it in `flake.nix`'s `drmKmodSrc`), once the x86 drivers that share that source are
  either adapted or given their own unpatched copy.
- `patches/drm-kmod/0008` only together with a GPL-2.0-compatible kext that sets
  `SF_DRM_GEM_SHMEM`.
