# m1-lkpi-extras: the remaining Linux kernel API for the Asahi G13 driver (SF-F item F11)

Status: **build evidence only.** Everything here compiles for the arm64 kext target and
links into one relocatable object, and the xarray code passes randomised host tests.
Nothing has been loaded into a kernel or run on hardware.

What this directory answers: for each kernel API in F11's list, does the G13 path of
Omacom's driver (`sources/gpu-plan/linux-aurora/drivers/gpu/drm/asahi/`, common code plus
T8103; 59 files, 29,653 lines; `g15/`, `g17/`, `m3/` excluded) use it, what does the layer
(`src/Kernel/fbsdkpi`: FreeBSD LinuxKPI plus SiliconForest glue) have underneath, and what is
the smallest correct thing to add.

Result: of the kernel-crate modules in scope, the G13 path needs four that do not compile
on `../m1-rust-kext/lkpi` as it is (`xarray`, `module_param`, `mm`, `seq_file`). All four
compile with the additions. (`scatterlist` was a fifth; that directory added its own helper
and `u64` address types while this was being written, so it is served there.) `module!` did not produce valid Mach-O and now does. Fourteen of the
driver's direct `bindings::` symbols in scope were missing and now exist (the shrinker,
workqueue, `capable` and `CTR_EL0` rows below). Two findings are left for
other tickets (bottom of this file).

## Decision table

Driver paths are relative to `drivers/gpu/drm/asahi/`. "Lines" is what this directory adds.

| API | Used by the G13 path | In the layer | Decision | Lines | Behaviour lost |
|---|---|---|---|---|---|
| `xarray` | **yes.** Three per-file arrays `file.rs:264-268`, `Alloc1` `file.rs:329-343`; `reserve_limit` + `fill` `file.rs:576,636,1322,1345`; `get` `file.rs:707,857,1022`; `find` `file.rs:1125`; `remove` `file.rs:667,1268,1358` | partial: `linux/xarray.h`, `linux_xarray.c` (446 lines, a wrapper on its radix tree). 32-bit index; `-EEXIST`/`-ENOMEM` where Linux says `-EBUSY`; no `xa_find`, `xa_find_after`, `__xa_cmpxchg`, `xa_trylock`, `XA_ZERO_ENTRY`; `xa_for_each` stops at a reserved entry | (b) new C on the same `struct xarray`: `c/sf_xarray.c`, 9 entry points with Linux 7.2 semantics; `helpers/xarray.c` routes the unmodified `rust/kernel/xarray.rs` to them | 261 C + 51 header + 87 helper | Indices above `UINT32_MAX` refused (driver ids are `u32`). No marks, no multi-index, no RCU readers (lookups take the array lock, a recursive sleepable mutex here). `__xa_alloc` costs one tree descent per occupied index before the first gap; the arrays hold VMs, queues and timestamp objects of one file, tens of entries. LinuxKPI's own functions are unchanged, so its `xa_for_each` still stops at a reservation (no C user reserves) |
| `maple_tree` | **no.** Only user is `vm/range.rs` (125 lines), and `mod vm` is `#[cfg(CONFIG_DRM_ASAHI_MAPLE_TREE)]` (`asahi_core.rs:42`), a symbol no Kconfig in the tree defines. GPU VA bookkeeping is `drm::mm` (`alloc.rs:697,740`, `mmu.rs:375`) and `drm::gpuvm` (`mmu.rs:41`) | stub struct (`drm/include/linux/maple_tree.h`) | (a) nothing. A port is `lib/maple_tree.c`, 7,050 lines, for 125 lines that are never compiled | 0 | none |
| `time::hrtimer` | **no** (0 references) | `linux/hrtimer.h`, a callout wrapper | (a) nothing | 0 | none |
| `module_param` | **yes.** `module!` with 28 parameters `asahi_core.rs:83-207`; 9 read on the G13 path: `debug.rs:94`, `initdata.rs:787`, `gpu.rs:305`, `queue/mod.rs:557`, `workqueue.rs:669`, `mmu.rs:3304`, `channel.rs:644`, `driver.rs:329`, `memacct.rs:167-172` | `kernel_param` with another layout; nothing reads a `__param` section | (c) Rust: same `ModuleParamAccess<T>::value()`; values come from xnu boot-args `asahi.<param>=value`, read once at module init through Linux's own parser. `rust/patches/1001` | +73 Rust, 1 helper in `sf_extras.c` | No runtime change, no sysfs (on Linux these are `perm: 0` and read-only too). A value that does not parse is logged and the default stays, as for a built-in on Linux |
| `module!`, `THIS_MODULE` | **yes.** `asahi_core.rs:65,83`; `driver.rs:117`, `queue/mod.rs:265` | n/a | (c) same patch: no `__param` and `.initcall6.init` statics (not Mach-O section specifiers, so `module!` did not compile for this target). `__asahi_init()`/`__asahi_exit()` stay `#[no_mangle]`; the kext's start/stop call them. `THIS_MODULE` is the built-in form (null) | (in 1001) | No module reference counting (LinuxKPI's is a no-op as well) |
| `module_platform_driver!` | no (the driver registers by hand, `asahi_core.rs:72-76`) | | nothing | 0 | |
| `cpufreq`, `freq_qos`, `devfreq`, `opp` | **no.** OPP tables are read as device-tree nodes through `of` (`hw/mod.rs:468-481`) | `linux/cpufreq.h` is an empty stub | (a) nothing | 0 | none |
| shrinker | **yes**, C API directly: `mmu/residency.rs:248-324`, `file.rs:67,638` | complete: `linux/shrinker.h`, `linux_shrinker.c`, but as macros, and driven only by FreeBSD's `vm_lowmem` event, which nothing in this layer raises | (b) three helpers over LinuxKPI's implementation | in `sf_extras.c` | **A registered shrinker is never called.** The driver does not release idle GPU pages under memory pressure. Register, unregister and free work |
| `mm` (`Mm`, `RssCounter`) | **yes.** `memacct.rs:45-47,275,324,358` (`mm_alloc`/`mmput` at `:519,540` are KUnit-only) | `mm_struct` is two counts and a lock; every task shares one static instance (`pd/lkpi/sf_linux_current.c:62`) | (c) `rust/patches/1002` keeps `Mm`, `MmWithUser`, the counters; compiles out `mm::virt`, mmap and VMA locks. `helpers/mm.c` | +16 Rust, 54 helper | **RSS attribution is a no-op** (`add_mm_counter` does nothing, `get_mm_counter_sum` reads 0). The driver's own limits do not depend on it |
| `mm::virt`, vma | **no** | absent | compiled out (1002) | | none |
| `fs`, `file` | **no** (the driver's `File` is `drm::file`) | partial | (a) nothing; stays off | 0 | none |
| `cred` | only `capable(CAP_SYS_NICE)`, `file.rs:1314` | `capable()` is a static inline over `priv_check()`, which here is "effective uid 0" | helper | in `sf_extras.c` | Any capability means root |
| `task`, `pid` | `current!().mm()` `memacct.rs:324` | in the integrator's core tier (its patch 0001 gates `mm()` on `sf_mod_mm`) | nothing beyond enabling `mm` | 0 | pids name threads only |
| `seq_file` | **yes.** `stats_raw.rs:32,453-457,493` | `seq_printf` is a macro over the kernel printf, which does not know `%pA` | helper formatting `%pA` through `rust_fmt_argument()` | in `sf_extras.c` | none |
| `debugfs` | **no** Rust module. `drm_debugfs_add_file` is behind `#[cfg(CONFIG_DEBUG_FS)]` (`stats_raw.rs:429`), not set here; the show callback is still compiled and reads `drm_debugfs_entry.file` (`:461`) | n/a | bindgen fix: `#define file linux_file` renamed that member (and `drm_syncobj.file`, `fd.file`); named back | 5 nix | The `asahi_stats` debugfs file does not exist |
| `scatterlist`, `sg_table` | **yes**, through `drm::gem::shmem::SGTable`: `gem.rs:118-122,436`, `mmu/bind.rs:20,84`, `mmu.rs:2053` | present; three functions are inlines/macros | (a) nothing here: `../m1-rust-kext/lkpi/helpers/scatterlist.c` and its `resource_size_t = u64` bindgen line cover it | 0 | none |
| `page` | **yes.** `pgtable.rs:215,236,436,664,831`, `mmu.rs:2255,3142`, `gem.rs:457` | integrator's core tier | (a) nothing | 0 | |
| `workqueue` extras | **yes.** `cleanup.rs:35-47,115`: `alloc_workqueue_noprof`, `WQ_UNBOUND`, `WQ_MEM_RECLAIM` | `alloc_workqueue` is a macro; flags are ignored | helper + 3 constants | in `sf_extras.c` | No rescuer thread (`WQ_MEM_RECLAIM`); flags ignored |
| `rbtree`, `list` | **no** direct use (`list` is core tier) | | nothing | 0 | |
| `revocable` | through `Devres` (`regs.rs:20,567`) | compiles already | (a) nothing | 0 | |
| `firmware` | **no** (blobs come from the device tree, `gpu.rs:918-1063`) | | nothing | 0 | |
| `clk`, `regulator` | **no** | | nothing | 0 | |
| `pm`, runtime PM, `pm_domain` | **no** references in the G13 files | | nothing | 0 | Power is whoever starts the coprocessor (F10/F13) |
| `delay` | `fsleep` `channel.rs:169,204`, `mmu.rs:1997` | core tier | (a) nothing | 0 | |
| `bitmap`, `bits` | **no** | | nothing | 0 | |
| `str`, `CStr`, `CString` | yes, pure Rust (`alloc.rs:724`, `channel.rs:527`) | core tier | (a) nothing | 0 | |
| `jump_label`, `tracepoint` | **no** in the G13 files | | nothing | 0 | |
| `kunit` | `#[kunit_tests]` at `gem.rs:482`, `memacct.rs:381,545` | | (a) nothing: the macro puts the module under `cfg(CONFIG_KUNIT = "y")`, not set | 0 | The suites are not built |
| error codes | 17 used: `EINVAL EIO EOVERFLOW ENODEV ENOENT ENOMEM EFAULT EBUSY ERESTARTSYS ERANGE ENOSPC ETIMEDOUT EAGAIN EACCES ENODATA ECANCELED E2BIG` | all 17 exist; none is among the 41 gated ones | (a) nothing | 0 | Values are xnu's: `EAGAIN` 35, `ETIMEDOUT` 60, `EOVERFLOW` 84, `ECANCELED` 89, `ENODATA` 96. `ERESTARTSYS` is 512 and must not reach userspace; `linux_get_error()` (`linux_compat.c:949`) converts it for LinuxKPI file operations, the SFDRMDri ioctl path is unchecked |
| uapi `asahi_drm.h` | **yes** (`driver.rs:122-150`, `file.rs`) | absent | the header is taken from the driver's tree by path at build time, with a one-line `drm.h` forwarder | 6 | none. `DRM_IOCTL_ASAHI_GEM_MMAP_OFFSET` comes out as `0xC0106446`, the Linux number |
| arm64 `read_sanitised_ftr_reg(SYS_CTR_EL0)` | **yes.** `pgtable.rs:62-70` | absent | helper reading `CTR_EL0` on the calling CPU + 6 constants | in `sf_extras.c` | Not merged over CPUs (see risks) |
| `si_meminfo`, `sysinfo` | **yes.** `memacct.rs:161-163` | present | nothing here | 0 | **Reports 4 GiB on every machine** (`pd/lkpi/sf_lkpi_kern.c:76`); see "Left for other tickets" |

Totals: 366 lines of C and headers for the layer, 275 lines in 3 helper files (28
`rust_helper_*`), +89 lines of Rust in 2 patches, 49 lines of bindgen additions.

## Files

```
c/sf_xarray.c, c/include/linux/sf_xarray.h   add to the layer (BSD-2-Clause)
c/include/sf_lkpi_extras.h                   constants for bindgen and the helpers
helpers/xarray.c mm.c                        replace Linux's rust/helpers files of those names
helpers/sf_extras.c                          new: shrinker, workqueue, capable, sysreg, seq_printf, boot-arg
bindgen/sf_bindings_extra_add.h              appended to lkpi/include/sf_bindings_extra.h
bindgen/bindgen_parameters_add               appended to lkpi/bindgen_parameters_pd
bindgen/uapi/drm/drm.h                       the "drm.h" asahi_drm.h includes
rust/patches/1001, 1002                      against Linux 7.2.11 rust/, no overlap with lkpi/patches
rust/probe/lib.rs                            compile-and-link probe mirroring the driver's call sites
tests/                                       host test and stand-in headers
default.nix, Makefile, tools/report.py
```

## For the integrator, in order

State on 2026-11-10: `../m1-rust-kext/lkpi` has taken steps 1-7 (its
`extras = ../../m1-lkpi-extras`, `patches-layer/0007`, the `linux_file` rename, and the four
modules in `modules.agx`). Steps 8-10 are for the driver kext (F13). One thing to know
about how it imports this directory: `${extras}` is the whole directory, so any edit here,
this README included, changes that store path and rebuilds its layer tree, bindings and
every crate. Referring to the sub-paths it uses (`${extras + "/helpers"}` and so on) would
limit that to real changes.

1. **Layer C.** Copy `c/sf_xarray.c` to `src/Kernel/fbsdkpi/sf/lkpi/` and add it to the
   LinuxKPI source list in `fbsdkpi.cmake` (next to `linux_xarray.c`); copy
   `c/include/linux/sf_xarray.h` to `pd/lkpi-override/linux/`. No new flags. It needs only
   `radix_tree_*`, the mutex and `pause_sbt` from the layer (`clib`'s
   `nix-support/undefined`).
2. **Headers for bindgen and helpers.** Put `c/include/sf_lkpi_extras.h` in
   `lkpi/include/`. Append `bindgen/sf_bindings_extra_add.h` to
   `lkpi/include/sf_bindings_extra.h` and `bindgen/bindgen_parameters_add` to
   `lkpi/bindgen_parameters_pd`.
3. **uapi.** Make `<uapi/drm/asahi_drm.h>` resolve: an include directory containing
   `uapi/drm/asahi_drm.h` (the driver tree's `include/uapi/drm/asahi_drm.h`) and
   `uapi/drm/drm.h` (`bindgen/uapi/drm/drm.h`). Linux's `uapi_helper.h` already includes it.
4. **Helpers.** Copy `helpers/*.c` into `lkpi/helpers/`. Two have the names of Linux's
   own files (`xarray.c`, `mm.c`), which is how the overlay replaces them; none collides
   with a file there today (the recipe here fails if one does, which is how the
   `scatterlist.c` that directory added on its own was noticed and ours dropped).
5. **bindgen post-processing.** Next to the existing `sf_fbsd_free` rename in
   `bindingsGen`, add `sed -Ei 's/\bpub linux_file:/pub file:/'` on
   `bindings_generated.rs` (three members: `drm_debugfs_entry.file`, `drm_syncobj.file`,
   `fd.file`).
6. **Rust patches.** Copy `rust/patches/100[12]-*.patch` into `lkpi/patches/`. They sort
   and apply after `0001`-`0006` and touch `kernel/module_param.rs`, `macros/module.rs`
   and `kernel/mm.rs`, none of which those patches touch.
7. **Modules.** Add `"xarray" "module_param" "mm" "seq_file"` to the module list the
   driver's kernel crate is built with (`modules.agx` there; `extrasModules` here).
8. **Driver build.** Set `RUST_MODFILE` in the environment of the driver crate's rustc
   (`module!` reads it). Do not set `--cfg MODULE` or `CONFIG_DEBUG_FS`/`CONFIG_KUNIT`.
9. **Kext glue.** After `sf_fbsd_init`, call `int __asahi_init(void)` from the kext's
   start routine and `void __asahi_exit(void)` from stop; export both from the Rust object
   (`rust.mkKextObject { exports = [ "___asahi_init" "___asahi_exit" ]; }`).
   `__asahi_init` reads the boot-args before the driver's own init.
10. **Boot-args.** Parameters are `asahi.<name>=<value>`, e.g. `asahi.debug_flags=0x10`.
    The kext needs `PE_parse_boot_arg_str` from xnu (declared in `pexpert/pexpert.h`).


## Rebuild and test

```
export PATH=$HOME/.nix-profile/bin:$PATH
F="--extra-experimental-features nix-command --extra-experimental-features flakes"
cd integrations/siliconforest/m1-lkpi-extras
nix-build $F -A clib --no-out-link        # (i) C for arm64 with the layer's flags
nix-build $F -A report --no-out-link      # (ii) matrix before/after, report.md
nix-build $F -A probe --no-out-link       # probe compiled, linked, symbols classified
nix-build $F -A kernelAgx --no-out-link   # kernel crate on lkpi's full driver module list
nix-build $F -A hostTests --no-out-link   # make test in the sandbox
make test                                 # the same on this machine (about 35 s)
```

Because `../m1-rust-kext/lkpi` imports this directory, "with the additions" is that
directory's own derivations. `default.nix` here measures: it compiles `c/` on its own, runs
the module matrix on that directory as it is and again with this directory substituted out
("before"), builds the whole kernel crate and the probe, and runs the host tests. It relies
on these attributes of `../m1-rust-kext/lkpi`: `bindingsGen`, `rustTree`, `hostMacros`,
`ffi`, `build_error`, `pin_init`, `zerocopy`, `bindings`, `uapi`, `kernelWith`,
`linuxCrate`, `pm`, `matrixOnly`, `cflags.{cc,sysroot}`,
`modules.{core,plat,agx,agxConfig}`, `rust.mkKextObject`, `finalTree` (else `tree`). For
"before" it finds what to replace by the names that recipe gives its derivations
(`sf-lkpi-rust-helpers-overlay`, `sf-lkpi-extras-include`) and this directory's name; if
one is no longer referred to, evaluation fails and says which. Nothing is pinned by store
path.

### Matrix (`-A report`), modules the G13 path uses

| module | before | with the additions |
|---|---|---|
| `xarray` | 21 errors | compiles |
| `module_param` | 11 errors | compiles |
| `mm` | 49 errors | compiles |
| `seq_file` | 1 error | compiles |
| all four together | 82 errors | compiles |
| `scatterlist` | compiles (that directory's own helper) | compiles |

Not used by the G13 path, unchanged, listed so the status is on record: `maple_tree`
(17 own errors), `time::hrtimer` (15), `cpufreq` (48), `fs` (16), `cred` (6), `debugfs`
(8), `rbtree` (18), `bitmap` (10), `regulator` (3), `pid_namespace` (5), `security` (8)
stay blocked; `revocable`, `firmware`, `clk`, `opp`, `jump_label`, `tracepoint`, `kunit`
compile as they did (several are empty without their `CONFIG_`).

### Whole kernel crate (`-A kernelAgx`)

The kernel crate with `../m1-rust-kext/lkpi`'s full module list for the driver
(`modules.agx`: driver core, DRM wrappers, RTKit, mailbox) plus `xarray`, `module_param`,
`mm` and `seq_file`, against the bindings and Rust tree with the additions: compiles to an
rlib. So the additions coexist with everything else the driver is built on.

### Probe (`-A probe`)

`rust/probe/lib.rs` repeats the driver's call sites for every item above (each function
cites the driver line). It is built as a staticlib against the kernel crate on
`modules.plat` plus the four modules and linked (`ld -r`) with `libhelpers.a` and
`libsflkpiextras.a`. The build fails if a `rust_helper_*` or `sf_xa_*` symbol is left
undefined or one of 23 expected helpers was not pulled in. Result: links; 135 symbols are
left for the kernel collection, 115 of them defined in the layer's sources, the rest are
libc-style kernel functions, layer macros/variables, and `PE_parse_boot_arg_str`
(`undefined-where` in the output). Sections are only `__TEXT`/`__DATA` standard ones plus
the layer's `fbsd_sysinit`. This is not a load test.

### Host tests (`make test`)

`tests/test_xarray.c` links the layer's unmodified `linux_radix.c` and `linux_xarray.c`
with `c/sf_xarray.c` (only the headers beneath them are stand-ins, `tests/shim/`) and
compares against a flat-table model written from Linux's documented behaviour: load,
store, insert, erase, raw compare-exchange, id allocation with random limits, find,
find-after, out-of-range and unstorable values, on plain, `ALLOC` and `ALLOC1` arrays,
interleaved with LinuxKPI's own `xa_store`/`xa_erase`/`xa_insert` on the same array. Node
allocations fail at random (0, 3 % and 30 %); a failed operation must leave the array
unchanged and a sleeping one must succeed after the wait. A fixed scenario replays the
Rust wrapper's sequence (reserve, fill, double fill, release, reuse).

Last run: 120 seeds x 20,000 steps, 18,214,721 checks, 234,155 node allocations (5,568
failed by injection, 3,909 waits), 0 nodes live at exit, under ASan and UBSan. Nine
hand-made mutations of `sf_xarray.c` (wrong error code, off-by-one limit, reserved entries
iterated, `ALLOC1` handing out 0, and so on) were each caught.

## Not implemented, and why

- **maple tree, hrtimer, cpufreq/freq_qos/devfreq/opp, fs, cred, debugfs, rbtree, bitmap,
  firmware, clk, regulator, runtime PM, jump labels, tracepoints:** the G13 path does not
  reach them (table above). They stay switched off.
- **Shrinker reclaim:** would need xnu memory-pressure notifications wired to LinuxKPI's
  `vm_lowmem` event. Not needed for first render; needed before the driver is trusted
  under memory pressure.
- **RSS attribution:** there is no per-process `mm_struct` and no consumer.
- **KUnit suites:** no KUnit runtime.

## Left for other tickets

1. **`si_meminfo()` says 4 GiB.** `sf_lkpi_physmem()` (`pd/lkpi/sf_lkpi_kern.c:76`) is a
   constant. The driver derives its GPU memory limits from it (`memacct.rs:161-168`: 67 %
   device-wide, 50 % per client), so they come out as 2.68 GiB and 2 GiB whatever the
   machine has. The fix belongs in the layer's xnu seam (`max_mem`,
   `osfmk/mach/vm_param.h:237`); it is not done here because whether a kext in the
   collection can bind that symbol cannot be checked without building a collection.
2. **`AS_INACCESSIBLE`/`AS_UNEVICTABLE` (`gem.rs:95-112`).** There is no `address_space`
   (`linux_file` has no `f_mapping`), so `set_mapping_held()` needs a SiliconForest branch in
   the driver. `../m1-drm72/README.md` already assigns this to F13.

## Risks, most serious first

1. **None of this has run.** In particular the boot-arg path (`PE_parse_boot_arg_str`
   binding in the collection), `seq_printf` with `%pA`, and `CTR_EL0` access from a kext
   are compile-and-link evidence only.
2. **xarray lock recursion.** `rust/kernel/xarray.rs` calls `xa_load`/`xa_find` with the
   array lock held (Linux takes only RCU there). This works because LinuxKPI's `xa_lock`
   is a recursive mutex in this layer (`MTX_RECURSE`, honoured by Haiku's `mutex.c`). If
   the integrator's lock patches change that, every `Guard::get()` deadlocks. The host
   test only models recursion depth.
3. **xarray lock is sleepable and dropped on allocation failure.** With a blocking `gfp`
   the lock is released while waiting for memory (as LinuxKPI and Linux do). Under a Rust
   `Guard` that means another thread can change the array during `reserve_limit`/`store`;
   the wrapper's contract allows it, the driver has not been audited for it.
4. **`CTR_EL0` is read on the calling CPU.** Correct only if both T8103 core types report
   the same `DminLine`. If they differ and the thread migrates, cache cleaning of GPU page
   tables could use too large a stride. Not checked on this machine.
5. **Memory pressure.** No shrinker callbacks and a 4 GiB RAM figure: the driver neither
   gives memory back nor sizes its limits from the real machine.
6. **Errno numbering.** Values are xnu's. Anything in the driver or Mesa that compares
   against Linux numbers, or an `ERESTARTSYS` (512) that reaches userspace, misbehaves.
7. **Following `../m1-rust-kext/lkpi`.** The "before" measurement re-instantiates that
   directory's derivations by string substitution. Renamed attributes or derivations break
   evaluation here (loudly; it happened twice while this was written). A patch there that
   edits `module_param.rs`, `macros/module.rs` or `mm.rs` would make 1001/1002 fail to
   apply in its own `rustTree`.
8. **`capable()` is "root".** A non-root DRM master still gets high priority through the
   driver's other check; a root process always does.
