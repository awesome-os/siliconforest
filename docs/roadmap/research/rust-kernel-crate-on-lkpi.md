<!-- Research input for the roadmap tickets; produced by a read-only research agent on 2026-11-09. Unverified on hardware. -->

Phase B builds through all four deliverables: `PDRustLKPI.kext` is in a kernel collection for you to boot, the core tier of the `kernel` crate compiles on LinuxKPI, and everything in the subsystem tier except `device_id` is blocked. Nothing here has booted.

## What to boot

All paths are under `/home/chrisk/src/darwin-lab/integrations/puredarwin/m1-rust-kext/`. Run from there:

```
export PATH=$HOME/.nix-profile/bin:$PATH
F="--extra-experimental-features nix-command --extra-experimental-features flakes"
nix-build $F build-lkpi-kext.nix --no-out-link   # kext
nix-build $F build-lkpi-kc.nix --no-out-link     # kernel collection
nix-build $F lkpi -A bindingsGen --no-out-link   # B1; also -A rustTree, kernel, object, matrix
```

- **Kernel collection:** `/nix/store/ag6b5xw2m5pjy9yaync1liysgddbd5g0-puredarwin-kc-arm64-0.1/kernel` (42120616 bytes, sha256 `0026e02e…`). It is the phase A collection (bridge, Wi-Fi, PDRustHello) plus PDRustLKPI.
- **Kext:** `/nix/store/1gcvv4v70f6y56kgp5m6n19m6dzpppks-puredarwin-rust-lkpi-0.1/System/Library/Extensions/PDRustLKPI.kext`
- **B1 output and tables:** `/nix/store/b8f5lhlkndxgngp69h80180gzivh9hi2-pd-lkpi-rust-bindings` (`status/*.tsv`)
- **B4 matrix:** `/nix/store/mx7yncifqya43liwldz8ax1w2ivzvd83-pd-lkpi-kernel-crate-matrix` (`matrix.md`, `errors/`)

`bindingsGen`, `kernel` and `object` passed `nix-build --check`. The phase A derivations are unchanged.

Predicted output, from reading the code (there is no host run this time). No boot-arg is needed.

```
PDRustLKPI: start
PDRustLKPI: LinuxKPI runtime up (mutexes, callouts, taskqueues, sysinits)
PDRustLKPI: rust: pr_info! through kernel::print: 42 0x2a hello
PDRustLKPI: rust: alloc: KBox=0x123456789abcdef0 KVec len=100 sum=4950 KVVec len=10000 sum=49995000 zeroed=true
PDRustLKPI: rust: alloc: KBox<u128> (align 16) = Err(AllocError) (kmalloc alignment here is 8)
PDRustLKPI: rust: sync: Mutex<u64> 1+2=3 SpinLock<u32> 5+2=7
PDRustLKPI: rust: sync: Arc value=5 ptr_eq=true drops after first/second drop=0/1
PDRustLKPI: rust: sync: Atomic<i32> fetch_add old=40 xchg old=42 now=7 Atomic<u64>=0xffffffffffffffff
PDRustLKPI: rust: error: from_errno(-22)==EINVAL:true to_errno=-22 to_result(-12)=Err(Error(12)) to_result(0)=Ok(()) EAGAIN=-35
PDRustLKPI: rust: time: Monotonic elapsed over fsleep(20 ms) within 15..2000 ms: true
PDRustLKPI: rust: workqueue: work item running, value=42
PDRustLKPI: rust: workqueue: queued=true Completion within 5000 ms=true result=43
PDRustLKPI: rust: sync: CondVar wait ended: flag set=true timed out=false
PDRustLKPI: rust: done, 0 failed checks
PDRustLKPI: ALL OK
```

How to read deviations:
- **Second line missing:** the fbsdkpi layer's own init (`pd_fbsd_init`) failed or hung before any Rust ran. This is the only kext in the collection that links that layer.
- **Failed check:** prints `rust: CHECK FAILED: <what>`, then `FAILED (n)`.
- **Waits:** every wait has a 5 s timeout, so a missing wakeup shows as `timed out=true` or `Completion … =false`.
- **Rust panic:** becomes a kernel panic through LinuxKPI `BUG()`.
- **`EAGAIN=-35`:** correct here; the layer uses XNU errno numbering.
- **Lock leak:** the test leaks a handful of XNU locks once (see gap 3).

The kc-builder log differs from phase A's by `dyld_stub_binder` plus three unresolved weak references: `_pd_usb_module`, `_pd_usb_register`, `_pd_usb_unregister`. Those come from the layer, not from Rust, and PDDRMmsm has the same three. They are bound to the panic trampoline but only reached on USB attach paths.

## How LinuxKPI/DRM is packaged
- **Not a shared kext.** Each kext statically compiles its own copy of Haiku's FreeBSD compatibility layer, FreeBSD LinuxKPI, PureDarwin glue and, for DRM kexts, the drm-kmod core. All of it uses `-fvisibility=hidden`, so nothing enters the collection's symbol namespace.
- **arm64 today:** the flake builds only `PDDRMmsm` and `PDDRMDri` (`drm-msm-arm64`).
- **drm-kmod source:** `freebsd/drm-kmod` rev `c16de0f9`, fetched by the flake as `drm-kmod-src`, with PureDarwin's removals and patch. It is passed to CMake as `PD_DRMKMOD_DIR`.
- **Flags:** 30 `-D`s, 27 include directories, 5 forced includes; no `-O` flag. They are written once in `lkpi/cflags.nix`. The kext build fails if LinuxKPI's `-D` set differs from what bindgen saw; it matched.

Three layer facts that matter later:
- LinuxKPI's `PAGE_SIZE` is 4096 on arm64.
- Every `kmalloc` is `IOMalloc` plus a size header: alignment 8, always blocking.
- C `printk` is only visible with the `fbsdkpi_debug` boot-arg. The Rust `_printk` helper I wrote logs unconditionally, except `pr_debug!`.

## B1: bindgen against LinuxKPI
bindgen 0.72.1 on libclang 21.1.8, both from the flake's nixpkgs.

| Input | Present and parses | Fails | Absent |
|---|---|---|---|
| `bindings_helper.h`, 94 headers | 58 (6 of them empty stubs) | 1 | 35 |
| `bindings_helper.h`, 52 constants | 12 | 40 | – |
| `uapi_helper.h`, 9 headers | 1 (`uapi/drm/drm.h`) | 0 | 8 |
| `rust/helpers`, 67 files | 25 unmodified | 27 | – |

- **Empty stubs that "parse":** `kunit/test.h`, `linux/cpufreq.h`, `linux/dma-direction.h`, `linux/fdtable.h`, `linux/input.h`, `linux/property.h`.
- **Fails:** `linux/usb.h` (`device_t` unknown).
- **Absent, relevant to the driver:** `drm/drm_gem_shmem_helper.h`, `linux/soc/apple/{rtkit,mailbox,pmp}.h`, `linux/io-pgtable.h`, `linux/cpumask.h`, `linux/cred.h`, `linux/of_reserved_mem.h`, `linux/hrtimer_types.h`, `uapi/drm/asahi_drm.h`. The rest are block, sound, iio, phy, binder and similar.
- **Helpers:** 15 of the 67 Linux files are replaced by PureDarwin versions in `lkpi/helpers/`, and 2 files are new (`pd_calls.c`, `print.c`); 641 lines of C in total. The result is 215 `rust_helper_*` functions.
- **Generated bindings crate:** 2440 functions, 845 structs, 6827 constants. It compiles.

Findings that will bite again:
- **Macros hide the API.** Most Linux functions are macros or inlines in LinuxKPI, so bindgen cannot see them; each one Rust calls needs a helper.
- **Hidden visibility hides functions.** bindgen skips hidden functions, so it runs without `-fvisibility=hidden`.
- **Macro renames leak into the bindings.**
  - `#define free pd_fbsd_free` renames the `free` member in 3 structs, including `drm_gem_object_funcs`.
  - `#define file linux_file` means `bindings::file` is currently FreeBSD's `struct file`, not LinuxKPI's. This is harmless while `fs` is off, and must be fixed with a blocklist and alias before `fs` or `debugfs` are enabled.
  - `page` and `wait_queue_entry` are already aliased.
- **`seqlock` name clash:** Haiku's `seqlock` typedef collides with LinuxKPI's `struct seqlock`; renamed for bindgen only.
- **`c_char` signedness:** `ffi::c_char` is `u8` (Linux builds with unsigned char) while `core::ffi::c_char` is `i8` on this target. `c"…".as_ptr()` therefore mismatches, in 3 places in `rust/kernel` and none in the driver.

## B2: `kernel` crate, core tier
Delta against pristine `rust/`: 136 mechanical one-line gates (95 module `#[cfg]`s, 41 Linux-only errnos) plus three patches totalling +41 −12 hand-written lines (`lkpi/patches/`).

| Module | Status |
|---|---|
| types, str, fmt, init/pin-init, list, bits, num, ptr, sizes, transmute, build_assert, std_vendor, ffi, safety, addr | unmodified |
| alloc (KBox, KVec, KVVec, Kmalloc/Vmalloc/KVmalloc) | Rust unmodified; `slab.c` and `vmalloc.c` replaced; alignment above 8 is refused |
| print | Rust unmodified; `KERN_*` overridden in bindgen; new `_printk` helper handles `%pA` |
| error | 41 errnos gated, plus `ENOIOCTLCMD` (it is FreeBSD's −3 here); `errname()` returns NULL |
| sync (Mutex, SpinLock, Arc, Completion, CondVar, atomics, refcount, RCU, barriers) | 1 line (`poll` gated, needs `fs`); atomic helpers reimplemented with compiler builtins |
| time | 1 line (`hrtimer` gated); `CLOCK_TAI` reads as realtime |
| prelude | +6 −9 (device macros and `UserPtr` only with their modules) |
| task | +14 −2 (mm, pid-namespace and credential methods gated) |
| page | +3 −1; `page.c` replaced |
| workqueue | +16 (delayed-work init, system queue names) |
| xarray | blocked: LinuxKPI's xarray has a 32-bit index and lacks `xa_find`, `xa_find_after`, `__xa_cmpxchg`, marks, `xa_trylock` and `XA_ZERO_ENTRY` |

Semantics that differ from Linux even though it compiles:
- `SpinLock` is a sleepable FreeBSD mutex and irqsave does not mask interrupts.
- There are no exclusive waiters.
- There is no freezer.

## B4: subsystem tier, compile-only
Each module was compiled on top of the core tier with whatever else it needed switched on. Only `device_id` compiles. "Own" means errors in the module's own files.

| Module | Own errors | Blocked by |
|---|---|---|
| device | 26 | 20 `fwnode_*` functions plus `__dev_fwnode` (`linux/property.h` is an empty stub); `_dev_printk` |
| driver | 3 | `device_driver.p_cb`, `acpi_of_match_device`, `acpi_device` |
| platform | 6 | `__platform_driver_register`, `get_device`, `platform_device_put`, `platform_get_irq_byname_optional`; `device_driver.{acpi_match_table,suppress_bind_attrs}` |
| of | 0 | dependencies only (device, io) |
| io | 24 | `readb`…`writeq_relaxed`, `memcpy_{from,to}io`, `resource_size` are macros; `__request_region`, `release_region`, `release_mem_region` and two `IORESOURCE_*` constants are absent |
| uaccess | 9 | `copy_{from,to}_user`, `_copy_*`, `strncpy_from_user`; 4 type errors |
| dma | 3 | `DMA_ATTR_MMIO`; 2 type errors |
| debugfs | 24 | `file.private_data`, `simple_open`, `default_llseek`, `noop_llseek`, `inode`; 10 type mismatches (the `file` alias issue) |
| devres | 14 | 7.x `devres_node` API absent |
| irq | 20 | 17 `IRQF_*` constants, `request_threaded_irq`, `free_irq`, `synchronize_irq` |
| maple_tree | 17 | LinuxKPI has only a stub struct; 6 functions and the `ma_state` layout are missing |
| module_param | 21 | `kernel_param`/`kernel_param_ops` layout differs |
| time::hrtimer | 15 | `hrtimer_setup`, `hrtimer_start_range_ns`, `hrtimer_forward`, `hrtimer_cancel`, mode enum, `hrtimer.node` |
| cpufreq | 48 | header is an empty stub |
| mm / fs / cred | 49 / 18 / 6 | largely absent in LinuxKPI |
| dma_fence | 2 | `dma_fence_init64`; `dma_fence_ops` still has the three fields removed upstream |
| drm::device | 1 | `__drm_dev_alloc` |
| drm::file | 2 | `drm_syncobj_set_wait_hint`; one `c_char` mismatch |
| drm::gem | 4 | `drm_gem_object.gpuva.lock`; `funcs.free` (the macro rename); `noop_llseek` |
| drm::sched | 17 | see version gaps below |
| drm::gpuvm | 27 | see version gaps below |
| drm::gem::shmem | 32 | no GEM shmem helper in drm-kmod at all |
| drm::driver, ioctl, mm, syncobj | 0 | dependencies only |
| soc::apple::rtkit | 17 | no C: 11 functions, 3 types |
| soc::apple::mailbox | 7 | no C: 4 functions, 2 types |

Where drm-kmod's 6.14 C API differs from what the 7.2 wrappers expect:
- `drm_sched_init`: 11 positional arguments here, `struct drm_sched_init_args *` in 7.2.
- `drm_sched_job_init`: 7.2 adds `u64 drm_client_id`.
- `enum drm_gpu_sched_stat`: 7.2's `RESET` and `NO_HANG` are missing here.
- `drm_sched_backend_ops`: no `cancel_job`, `owner` or `retain_job_on_enodev`.
- `drm_gpuvm_sm_map`: takes `(addr, range, obj, offset)` here, `struct drm_gpuvm_map_req *` in 7.2.
- Missing in gpuvm: `DRM_GPUVM_IMMEDIATE_MODE`, `drm_gpuva_unlink_defer`, `drm_gpuvm_bo_deferred_cleanup`, `drm_gpuvm_bo_put_deferred`, `drm_gpuvm_bo_unmap`, `DRM_GPUVA_REPEAT`, `drm_gpuva_op_map.flags`, `drm_gpuvm_ops.sm_can_merge_flags`.

Of the driver's 122 direct `bindings::` symbols, 44 exist. The 78 missing are the Apple RTKit, mailbox and PMP set, cpufreq and freq_qos, the shrinker API, `ioremap`/`memremap`, module refcounting, `sha256`, `of_reserved_mem_lookup`, `region_intersects`, arm64 sysreg constants, newer workqueue calls (`disable_work_sync`, `mod_delayed_work_on`, `WQ_*` enums), and a few mm calls.

## Ranked gaps
Sizes are my estimates, not measurements.

**LinuxKPI holes**
1. **Page size.** LinuxKPI is 4 KiB on a 16 KiB kernel, and the driver requires 16 KiB. The fix is one define plus an audit of the layer's page code (about 520 lines in `pd_linux_page.c` and the contiguous allocator).
2. **kmalloc.** Alignment is 8 and `GFP_ATOMIC` is ignored (always blocking). It needs an aligned, non-blocking backend in `pdos_malloc`; about 150 lines of C.
3. **Lock lifetime.** The layer's mutexes, spinlocks and wait queues allocate an XNU lock on first use and free it only in `*_destroy`, which Rust never calls. Every dropped Rust lock leaks one. Fix is about 30 lines of Rust (a drop hook per lock backend) or embedding the locks on the C side.
4. **Driver core.** fwnode properties, `devres_node`, `device_driver` fields and region requests. This is 400–800 lines of C if LinuxKPI is extended.
5. **io, irq, uaccess helpers.** Roughly 200 lines of helper C; mechanical.
6. **xarray and maple_tree.** Extend the xarray (about 400 lines) or port `lib/xarray.c` (about 2,400); port `lib/maple_tree.c` (about 7,000) or replace it.
7. **cpufreq/freq_qos, shrinker (newer API), hrtimer, module_param.** Each needs an implementation or a decision that the driver does without.
8. **bindgen fixes.** The `file` and `free` renames; about 10 parameter lines.

**drm-kmod version gaps (6.14 → 7.2)**
9. **Scheduler and GPUVM.** Bring `scheduler/*.c` (about 2,700 lines in 7.2) and `drm_gpuvm.c` (about 3,400) to 7.2, plus the small `drm_gem`, `drm_syncobj`, `drm_drv` and dma-fence changes. dma-fence here is FreeBSD's own implementation, so `dma_fence_init64` and the ops change are a patch there.
10. **GEM shmem helper.** Port `drm_gem_shmem_helper.c` (about 1,000 lines) onto PureDarwin's shmem file backend.

**Apple-specific C**
11. **RTKit and mailbox** (2,948 lines per the plan numbers), PMP, `io-pgtable` for the UAT, `of_reserved_mem`. PureDarwin's `of_*` over the Apple device tree exists (`pd_linux_of.c`, 613 lines), but the m1n1-synthesised GPU properties do not.

**Rust-side**
12. So far +41 −12 hand-written lines and 136 mechanical. Expect the `c_char` call sites (3) and the lock drop hook next.

## Is "unmodified crate on LinuxKPI" holding up
- **Core tier: yes.** The helper mechanism absorbs the fact that LinuxKPI is mostly macros, and the Rust delta stays tiny. The cost is on the C side (641 lines of helpers so far) and in three semantic gaps that compile fine: alignment, blocking allocation, and lock leaks.
- **DRM wrappers: yes in principle.** They have few errors of their own and all of them are version skew. Updating the C core to 7.2 looks cheaper than back-porting the wrappers, because the driver depends on the newer GPUVM and scheduler behaviour as well.
- **device / driver / platform / devres: no.** These wrap Linux driver-core internals that are newer than LinuxKPI and have little to do with how a device arrives here (an IOKit nub and the Apple device tree). I would write thin replacement modules with the same Rust surface over PureDarwin's platform glue rather than grow LinuxKPI's driver core.
- **RTKit / mailbox: thin replacement.** I would implement the roughly 15 `apple_rtkit_*` and `apple_mbox_*` C functions over the RTKit client that already exists from the Wi-Fi work, and keep the Rust wrappers unmodified, instead of porting Linux's drivers.
- **maple_tree, module_param, cpufreq: decide per use.** First check what the driver actually uses each for; replacement or removal is likely cheaper than a port.

The page size and kmalloc items come before any of this: they are properties of the layer that the driver cannot work around.