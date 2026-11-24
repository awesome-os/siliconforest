# m1-dcp: Omacom's display-controller (DCP) driver as a kext (SF-H)

Status: see `reports/sprint-010/` for what has run on the M1 (the coordinator records boots;
this directory holds recipes, sources and tests).

Source of truth: `sources/gpu-plan/linux-aurora` (`integration/aurora` at 6a08a08e),
`drivers/gpu/drm/apple`. Two patches against the pin (`patches/`, 9 changed lines): one
identifier that clashes with a LinuxKPI macro, and three stage-log lines.

## What is here

Offline external-display preparation includes the48-byte private request ABI
`include/sf_dcp_tunnel_request.h` and SFDcp-local immutable epoch bridge
`kext/sf_dcp_tunnel_bridge.c`. It calls pinned `apple_dcp_tb_dp_tunnel` with the
actual **local** connector; Linux device-node pointers never cross the kext
boundary. No native provider, route graph, generic PHY/mux consumer or external
connector is published by this helper. It is uninstalled and cannot light a display.

The future native provider must retain its real connector and owner, prove
power/shared-TBT-lease ownership and serialize requests before calling it.
Each epoch keeps one full64-bit generation and immutable callback identity,
admits synchronous activation before attach returns, allows matching provider
retries and blocks stale owner/generation/callback/context changes. Detach
prevents reactivation and closes callbacks after the actual driver returns;
failed retirement retains the failed epoch. Epochs are never rebound/recycled.
The pinned driver's callback-drain contract is a dependency, not a hardware
drain observation. Native provider teardown and power-loss ordering remain required.

`python3 host-test/tunnel-bridge.py` compiles and executes the actual bridge
under ASan/UBSan with an explicit fake Linux provider: bad ABI, owner/generation
identity, synchronous callbacks, matching retries, busy/address-in-use replies,
callback errors and failed/complete detach. Native SDK compilation is separate.
Bridge entries are retained against dead stripping so their direct Linux
target must resolve in SFDcp; no cross-kext success stub or unresolved private
KPI qualifies as a provider. Machine trials remain paused by the owner.

| Path | What |
| --- | --- |
| `include/sf_dcp_compat.h` | Force-included in front of every driver file: names Linux 7.2 has and the layer lacks or spells differently. Each entry says which |
| `tools/pin-headers.txt` | 30 headers the driver needs that the layer lacks or has as empty placeholders; taken from the pin's `include/` as they are at build time, not copied here |
| `tools/probe.sh <workdir> [file ...]` | Syntax-checks the 29 module sources against the layer with the layer's own C flags; prints errors per file |

## Measurement (M-a)

`tools/probe.sh` over the 29 objects of `appledrm` without audio and tests:

| Step | Errors |
| --- | --- |
| layer as it is (every file stops at its first missing header) | 40 |
| missing headers present but empty | 429 |
| the pin's own headers | 325 |
| `drm_atomic_commit` = `drm_atomic_state`, workqueue/KUnit/flag names | 145 |
| `scoped_guard` label fix (layer patch 0016) | 113 |
| device-tree iteration macros, `DECLARE_COMPLETION_ONSTACK`, `alloc_ordered_workqueue` | 58 |

20 of 29 files have no errors, among them `afk.c`, `parser.c`, `iomfb.c`, `systemep.c`,
`ibootep.c`, `iomfb_v12_3.c`, `iomfb_v13_3.c`, `iomfb_v14_7*.c`, `iomfb_h17p.c`, `connector.c`,
`dcp-hdmi.c`, `dptxep.c`, `apple_resume.c`.

The 58 that remain, by file:

| File | Errors | What is missing |
| --- | --- | --- |
| `dcp.c` | 5 | `iommu_domain.pgsize_bitmap` (2), `SYSTEM_SLEEP_PM_OPS`, `MODULE_DEVICE_TABLE(of, ...)`, `platform_driver.driver_managed_dma` |
| `apple_drv.c` | 12 | `for_each_*` device-tree loops not yet covered, `drm_gem_fb_create`, `file_operations.get_unmapped_area`, module macros |
| `plane.c` | 9 | `DRM_FORMAT_P230`, `DRM_FORMAT_MOD_APPLE_INTERCHANGE` (the pin's `drm_fourcc.h`) |
| `dcp_backlight.c` | 7 | `backlight_ops.controls_device`, `backlight_properties.state`, `BACKLIGHT_PLATFORM`, `BACKLIGHT_SCALE_LINEAR`, `BL_CORE_SUSPENDED` |
| `iomfb_v26_6.c` | 2 | the same two backlight constants |
| `dcpext_drm.c` | 5 | shadow-plane helpers (`drm_gem_atomic_helper`), `drm_gem_fb_create_with_dirty` |
| `dcpext_scanout.c` | 4 | `pgsize_bitmap`, `struct reserved_mem`, `dma-map-ops.h` clash |
| `j613_dcp_activate.c` | 13 | `struct of_changeset`, `uuid_t`, a scoped loop |
| `dcp-fabric.c` | 1 | `struct of_changeset` |

Syntax checking stops short of code generation and linking: the list of functions the layer
must provide (component framework, `of_graph`, GEM DMA and FB DMA helpers, atomic helpers at
7.2, backlight class, iommu) comes with M-b.

## Decisions so far

- `struct drm_atomic_commit` (7.2) is `struct drm_atomic_state` (the layer's DRM core, 6.14)
  under another name: one `#define` in the compat header, and no error names a field or
  function of it afterwards. Whether the atomic helper *functions* the driver calls exist
  with 7.2 signatures is a link-time question.
- Headers for subsystems J293's internal panel cannot reach (mux, phy, Type-C, GPIO, syscon)
  are the pin's declarations; their functions will be stubs, each listed with the call site
  that shows it unreachable, once the link shows which are referenced.

## Needed from other directories

- `m1-dcp-dart`: `struct iommu_domain` with `pgsize_bitmap`, `IOMMU_READ`/`IOMMU_WRITE`,
  and the `iommu_*` functions `dcp.c` and `dcpext_scanout.c` call.
- `m1-gpu-dt` / the layer's `of`: `of_find_compatible_node`, `of_get_next_available_child`,
  `of_prop_next_u32`, `of_graph_*`, `struct of_changeset` (only `j613`/fabric use it),
  `struct reserved_mem`.
- Layer (`m1-rust-kext/lkpi/patches-layer`, 0016 onward): `scoped_guard` label fix (0016,
  done); the device-tree loop macros, `DECLARE_COMPLETION_ONSTACK` and the 7.2 names now in
  `sf_dcp_compat.h` belong in the layer's headers once the kext builds.

## Kext and collection (M-b, M-c)

```
nix-build build-kext.nix     SFDcp.kext + SFDRMDri.kext ($out/nix-support: undefined, SFDcp.map)
nix-build build-kc.nix       bridge collection + SFGpuDT + SFDRMDri + SFDcp (no Wi-Fi, no SFAgx)
tools/regen.sh setup|split   regenerate patches/ from an edited copy of the driver
```

Built into the kext: 27 driver files as at the pin (+ the two patches); from the pin as they
are `drm_gem_dma_helper.c`, `drm_fb_dma_helper.c`, `drm_gem_framebuffer_helper.c`,
`drm_simple_kms_helper.c`, `drivers/base/component.c`; the layer; the DRM core (m1-drm72);
m1-gpu-rtkit's RTKit port; m1-dcp-dart. SFDcp imports no symbol that SFAgx does not also
import, and the collection reports no unresolved symbol beyond the base collection's.
`--arg agx true` (SFAgx in the same collection) does not fit: 0x64000 bytes over.

Not built, with the reason J293 does not reach them (`kext/sf_dcp_linux.c`, part 3, `SF_STUB`):

| File | Entry points stubbed | Why unreachable here |
| --- | --- | --- |
| `dcpext_scanout.c`, `dcpext_drm.c` | `dcpext_scanout_terminal/requested/invalidate/link_restored/register/fault` | later-chip external scanout; callers test `dcp->external` or are firmware 14.7 paths. The current published tree has only internal `apple,dcp`; J293 also has physical dcpext. Its T8103 external output uses the normal IOMFB path and still needs native topology, PHY/crossbar, DMA and tunnel ownership |
| `j613_dcp_activate.c` | `apple_j613_25g83_coldplug`, `apple_t8122_25g83_board`, `apple_j613_25g83_clock_hz` | J613/T8122 with firmware 25G83 only |
| `audio.c`, `av.c` | none | `CONFIG_DRM_APPLE_AUDIO` off |

Reduced or refusing functions supplied by the kext: device links (recorded, not acted on),
devres groups (no rollback of a failed component bind), `disable_work`/`enable_work`
(cancel only), `drm_mode_size_dumb` (pitch = width x bytes per pixel), `of_changeset_*`
(refuse), `of_find_node_by_path` (NULL), `drm_gem_dma_mmap`'s `dma_mmap_*` (never called:
user mappings go through SFDRMDri), system sleep callbacks (compiled, not wired).

### Boot (not yet done)

Boot-args: `sfgpudt=2 sfgpudt_stringprops=1 sfdispdt=2 sfdcp=1`, optionally `sfdcp_debug=1`
(the driver's `dev_dbg` lines and every repeat of a callback) and `sfrtkit_debug=1`.

`sfdcp` values: 0 (default) not attached; 1 attaches, binds and starts the coprocessor, and
refuses every atomic commit with -EPERM; 2 also lets clients set modes and flip.

Lines to expect, all prefixed `SFDcp:` (driver messages are `SFDcp: <device>: ...`):
`LinuxKPI runtime up, sfdcp=1`; `dcp <node>: mailbox 0 is /arm-io/dcp (0), probing`;
`rtkit:` lines of the RTKit port; `device ... created for node piodma`;
`display-subsystem <node>: probing`; `endpoint 0x.. start requested (0)` per AFK endpoint;
`dcp_start: firmware compat id N, external 0`; `callback D... (in N, out M) context C:
answered` or `NO HANDLER` once per distinct callback. Which endpoints and callbacks appear,
and in what order, is not predicted here: this driver has not run on this layer.

Open before a boot is meaningful: the questions in `NEEDS-FROM-DART.md` (piodma device,
node matching, `of_find_compatible_node` over published nodes).

## Tests and other pieces added after the first bind

| Path | What |
| --- | --- |
| `test/dcp-flip-test.c`, `dcp-flip-test.nix` | KMS through the card node whose driver is `apple`: modeset, page flips with flip-complete events, `--wait read|poll|select|kqueue`, `--prime-from auto` (buffers of the GPU driver imported for scanout), `--scm-rights` (the GPU buffers come from a child process over a Unix socket), `--fence poll|select|kqueue` (waits on the CRTC out-fence sync_file and on a dma-buf holding it); prints where a flip's time goes (kernel event timestamps, commit call); `--dmabuf-mmap` (maps a buffer's dma-buf descriptor, writes through it, the owner reads back; with `--prime-from auto --scm-rights` across two processes and two drivers); `--in-fence` (the GPU driver's sync_file as the plane's IN_FENCE_FD) |
| `supervisor/darwin-dcp-{flip,prime,wait,scm,fence,newkernel,mmap,timing}-test.in` | guest scripts (DL43, DL44, DL45, DL47, DL48 lines; `newkernel` is DL45 + DL48 + DL47 in one boot; `mmap` is DL49, dma-buf descriptor mmap, and DL50, IN_FENCE_FD; `timing` is DL51, 3 x 20 s of flips for comparing `sflkpi_wq_highpri=0` against the default); `tools/dcp_boot.sh` stages them with `DCP_TEST=1|prime|wait|scm|fence|newkernel|mmap|timing`. All but the first three need the kernel below |
| `kext/sf_dcp_mmap.c` | user mmap of dumb buffers (SFDRMDri resolver) and the 3-argument `fb_create` adapter |
| `kext/sf_dcp_prime.c` | import of a dma-buf exported by another DRM kext, for scanout without a copy |
| `tools/symbolize.sh` | names kext addresses from a panic, from the link map kept with each build |
| `kernel-patches/`, `kernel.nix`, `kernel-swap.nix` | the kernel the collections here and in `m1-rust-kext` are built on; see below |
| `build-kc.nix --arg agx true` | SFAgx and SFDcp in one collection |
| `build-kc.nix --arg agx true --arg wifi true` | Wi-Fi, SFAgx and SFDcp in one collection; see "Space in the collection" |

## The kernel (sf_lfile descriptors)

dma-buf, sync_file and DRM syncobj descriptors are XNU `DTYPE_SFLFILE` files
(`bsd/kern/sf_lfile.c` in the SiliconForest tree). Two things about them needed kernel changes,
in `kernel-patches/`:

| Patch | What |
| --- | --- |
| 0001 | `fg_sendable()` accepts `DTYPE_SFLFILE`: the descriptors can be sent with `SCM_RIGHTS` |
| 0003 | `mmap()` of them (a dma-buf): `sf_lfile_ops.mmap_pages`, mapped through the device-pager path the DRM device nodes use; `MAP_SHARED` only |
| 0002 | select/poll/kevent wait on them: one selinfo for all linux files, `sf_lfile_wakeup()` (exported) for the layer to call, and a kevent filter `EVFILTID_SFLFILE` built like `spec_filtops` |

`kernel.nix` builds the lab's T8103 bring-up kernel with them: the flake's
`kernel-arm64-t8010-debug` with the machine config changed to T8103 and the source and three
toolchain paths taken from the derivation of the kernel every collection used so far
(`/nix/store/1ys1x1hz...-siliconforest-kernel-arm64-t8103-bringup-0.1`). Check that was done:
`nix-build kernel.nix --arg patched false` evaluates to the pinned derivation's environment
(every variable but `out`), and its kernel binary differs from the pinned one in 34 bytes: the
build date in the version string and the Mach-O UUID. A kernel build takes about six minutes
and is cached by nix.

`kernel-swap.nix` replaces the pinned kernel's path in a collection recipe's build phase;
`build-kc.nix` here and `../m1-rust-kext/build-agx-kc.nix` use it. The layer's patch 0022 calls
`sf_lfile_wakeup`, so a DRM kext built from it does not link against the pinned kernel.

**DEVELOPMENT configuration.** `kernel.nix --argstr kernelConfig development` (passed on by
`../m1-smp/kernel.nix` and `../m1-smp/build-kc-desktop.nix`) builds the same source and patches
as XNU's DEVELOPMENT configuration: optimised, without the DEBUG-only options, assertions
(`mach_assert`) still on. The bring-up tree refused anything but DEBUG for T8103 in three
places (the xnu CMake file and two assembly includes); `kernel-patches/0004` lets them accept
DEVELOPMENT and is applied only for this configuration, so the DEBUG kernel is unchanged.
RELEASE is refused (no serial console, no `-unsafe_kernel_text`). The kexts are the same
builds for both: no kext reference resolves in the DEBUG kernel and not in DEVELOPMENT, the
kexts are compiled without `DEBUG` or `MACH_ASSERT`, and the IOKit/libkern headers have no
DEBUG-only layout. The launcher's image check must accept `DEVELOPMENT_ARM64_T8103` (one line
of `normal_image.py` in the launcher tree, and its hash pin in `tools/pcie_iterate.sh`).
Boots smnd02 (session) and smnd03 (benchmark): single thread 864-888 MB/20 s against 616-632
on DEBUG, eight threads 7,056 against 5,056.

The wakeup is not per object: any linux file's wakeup makes every select/kevent waiter on a
linux file poll once more.

## Space in the collection

Measured on the collections of this state (`tools/` has no script for it; the numbers are
from the kexts' load commands inside the collection and from the link maps):

- The kexts have 16 MiB (`__PRELINK_TEXT` + `__PRELINK_INFO` must end before the kernel's
  `__TEXT`). A kext occupies its segments and its `__LINKEDIT`.
- Bridge collection 12,140,544 bytes; SFGpuDT + SFDRMDri + SFAgx add 2,686,976; SFDcp adds
  1,720,320 (1,179,648 of segments, 540,672 of `__LINKEDIT`); IO80211Family + SFBwfm add
  819,200. All three: 17,367,040, which is 753,664 too many.
- `__LINKEDIT` is almost all debug map: the objects are built with `-g`, and the linker then
  writes N_OSO/N_FUN stabs into the symbol table, which `-Wl,-x` does not remove. SFDcp:
  19,842 entries, 19,381 of them such; SFAgx 14,726 / 14,261. `strip -S` takes SFDcp's
  `__LINKEDIT` to 65,536 and SFAgx's to 81,920 bytes (819,200 saved) with `__TEXT`,
  `__DATA_CONST` and `__DATA` byte-identical. `slim` (the default since boots ws04 and ws05) does that, and Wi-Fi + SFAgx
  + SFDcp then passes the launcher's image gate with 69,632 bytes to spare.
- The same is true of every kext in the base: in the Wi-Fi + SFAgx collection the 55 kexts
  have 9.6 MiB of segments and 6.0 MiB of `__LINKEDIT` (112,715 symbol-table entries, 81,216
  of them local). That is where the room is; it is the base recipes' to take.
- One copy of the shared code instead of two: SFAgx and SFDcp each link 233.7 KiB of layer
  (Haiku compat, FreeBSD LinuxKPI, SiliconForest's), 367-373 KiB of DRM core (drm-kmod 6.14 +
  m1-drm72), 12.9 KiB of dma-buf/fence/resv, 21.1 KiB of DRM glue and 32.8 KiB of RTKit:
  about 670 KiB of code and data per kext, plus about 31 KiB of bss. A shared kext would save
  roughly that once, less its export table. It is not needed for space any more; the reason
  left for it is correctness across the two copies (a fence array or sync_file of one kext is
  not recognised by the other, see patch 0022's "Not done").

## Panel brightness and display sleep

**Brightness.** The driver's `dcp_backlight.c` registers a backlight class device,
`apple-panel-bl`, once the firmware has published the panel's level. Linux gives it files under
`/sys/class/backlight/apple-panel-bl`; here the same attributes are sysctls of the kext
(`kext/SFDcpSysctl.cpp`, semantics from the pin's `drivers/video/backlight/backlight.c` in
`kext/sf_dcp_backlight.c`):

| sysctl | |
| --- | --- |
| `kern.sf_backlight.name` | `apple-panel-bl` |
| `kern.sf_backlight.max_brightness` | 509 on the J293 (nits; `apple,max-brightness` 525 capped by the driver's table) |
| `kern.sf_backlight.brightness` | read: level last asked for; write (root): 0..max, `EINVAL` above |
| `kern.sf_backlight.actual_brightness` | the level the display coprocessor reports |
| `kern.sf_backlight.bl_power` | 0 on, 4 off; the level is kept |

All are `ENODEV` until the backlight is registered. A level is applied by re-presenting the
current frame, so it takes effect only while something is presenting (a compositor is).
`tool/sf-backlight.c` (built by `dcp-flip-test.nix` as `usr/local/bin/sf-backlight`):
`sf-backlight get | set <percent> | up | down | raw <level> | power on|off`.

**Display sleep.** Nothing SiliconForest-specific: an atomic commit with the CRTC's `ACTIVE` = 0
and `DRM_MODE_ATOMIC_ALLOW_MODESET` powers the panel down (`apple_crtc_atomic_disable` ->
`dcp_poweroff`), `ACTIVE` = 1 powers it up; the plane keeps its framebuffer. The legacy
connector `DPMS` property maps to the same in the DRM core.

Nodes are chosen by DRM driver name `apple` (`/dev/dri/card*`), never by number.

Tests: `tools/bl_boot.sh <name> <collection> supervisor/darwin-dcp-backlight-test.in` (DL52) and
`... supervisor/darwin-dcp-sleep-test.in` (DL53; `dcp-flip-test --dpms-cycles N`). Boots bl02
and bl03.

### Darwin connector hotplug notification (SF-AF source)

`sf_dcp_hotplug.c` observes Linux DCP's real hotplug notification, including
route removal. Its dedicated worker refreshes connector modes under the
same DRM lock used by the driver's relink path, then captures status/name/
preferred mode and increments `kern.sf_dcp.hotplug_seq`. The read-only
`kern.sf_dcp.connectors` exposes that cached text. Sysctl reads take only
the brief cache mutex; they do no detection, firmware call or modeset.
The sequence advances after the DRM mode/status cache and text are visible.
No fake connector, EDID or hotplug is introduced.

The aggregate T8103 DRM card is retained for the lifetime of this kext's
native platform copy, keeping queued work valid across cable loss. The
queue uses its own thread to keep an EDID/AUX wait off the RTKit and system
workqueue threads. DRM ioctls still use DRM locks: the requested ~50 ms
GetConnector/GetConnectorCurrent latency needs live measurement. DESK4
can poll the sequence and request the cached DRM view after it changes.
SDK compilation passes; USB04 does not include this newer DCP source and
no live external connector/notification or output is claimed.
