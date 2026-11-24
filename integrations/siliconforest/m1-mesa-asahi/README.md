# m1-mesa-asahi: Mesa's asahi driver for the SiliconForest M1 guest (SF-G items G1, G5)

Everything in this directory is produced as build and link evidence on the x86-64 Linux
builder. The off-screen test (section 3) has since been run on the M1 by the lead, under Linux
and under Darwin; those results are in `reports/sprint-010/gpu-first-render-001.md`, not here.
The panel demo (section 6) has **not** run on the target: it has only run on the builder, on
llvmpipe and on Mesa's asahi drm-shim (the real asahi driver with no GPU behind it).

## 1. Findings

### Source pins

- `sources/gpu-plan/mesa` is at `a1969111`, **not** at `147a62f6190`. The recipes here fetch
  `omacom/mesa` `147a62f619001f5f2588c7417c2b1fac3b0d93ad` (VERSION `26.3.0-devel`) as a GitHub
  archive tarball, sha256 `15b27d00ba2d857837001d357f189e64809b27937eba0dd722c9d28ec4ad0dcf`
  (`common.nix`). It is no longer the tip of `integration/aurora` (now `b7b03c5`).
- `asahi_drm.h`, kernel `linux-aurora` 6a08a08e vs this Mesa: the files differ textually, the
  ABI does not for anything the M1 uses. Compiling both and comparing `sizeof` of every
  `struct drm_asahi_*`, every `DRM_IOCTL_ASAHI_*` number and the offsets of all
  `drm_asahi_params_global` fields gives three differences, all size-negotiated tails:

  | struct | kernel | Mesa | how it is negotiated |
  |---|---|---|---|
  | `drm_asahi_params_global` | 616 | 600 | kernel copies `min(asked, known)`; tail is `memory_limit`, `client_memory_limit` |
  | `drm_asahi_cmd_render` | 448 | 320 (+ wrapper structs to 448) | `drm_asahi_cmd_header.size`; on G13 Mesa sends **240** |
  | `drm_asahi_cmd_compute` | 200 | 128 (+ wrapper to 200) | same; Mesa sends **128** |

  All 12 ioctl numbers and all other struct sizes are identical.

### What SiliconForest already has for DRM user space

| Piece | Where | State |
|---|---|---|
| `/dev/dri` character device for every DRM kext | `src/Kernel/Extensions/SFDRMDri/SFDRMDri.cpp`, `src/Kernel/fbsdkpi/drm/src/sf_drm_cdev.c` | in tree, used by the Adreno (`SFDRMmsm`) and x86 drivers |
| libdrm port | `nix/pkgs/x11/libdrm.nix` + `src/Libraries/libdrm/siliconforest-dri.patch` (207 lines) on libdrm 2.4.133; flake output `libdrm-dri-arm64` | builds here unchanged; has the platform-bus lookup asahi needs |
| Mesa cross-build | `nix/pkgs/mesa/mesa.nix` (upstream 26.1.6; freedreno/radeonsi/nouveau variants, LLVM, Wayland) | no asahi variant; its `--replace-fail` edits target 26.1.6 |
| GEM mmap on the device fd | `sf_cdev_mmap_register` in `xnu/bsd/kern/kern_mman.c` | in tree |
| Same-open-file test (Linux `kcmp`) | `fcntl(fd1, F_SF_SAMEFILE /* 0x5044 */, fd2)` | in tree |
| Off-screen GLES test | `src/Userspace/sfgltri` (surfaceless EGL, one triangle) | in tree, flake output `sfgltri-msm-arm64` |
| Host shader tools for a cross build | `mesa-pvr-native-tools` (Mesa 26.0.3) | wrong Mesa for asahi; `mesa-native.nix` here builds them from the pinned source |

### Conventions a Mesa winsys must follow on this OS (read from source)

| Topic | SiliconForest | Difference from Linux |
|---|---|---|
| Node names | `/dev/dri/renderD<N>`, `/dev/dri/card<N>`; N = the kext's DRM minor + its provider index, so numbering is system-wide | same names; never assume 128 belongs to the asahi kext. The test scans 128..135 for the name `asahi` |
| major/minor | one dynamic major (`sysctl kern.sf_dri.major`); `dev_t` is 8-bit major, 24-bit minor; an *open* node is a clone whose minor is `(slot << 8) \| drm_minor` | `fstat(fd).st_rdev` differs from `stat(path).st_rdev`; compare `minor & 0xff` (libdrm's patch does). No `<sys/sysmacros.h>`: `compat/sys/sysmacros.h` |
| Enumeration | no sysfs, no udev. `sysctl kern.sf_dri.devices`: one line per minor, `"<minor> platform:<of full_name> <first compatible>"` for device-tree devices, `"<minor> pci:... vendor device ..."` otherwise | libdrm's `drmGetDevices2`/`drmGetDevice2` are patched to read it and report `DRM_BUS_PLATFORM`. Mesa's EGL device list and loader need nothing else |
| ioctl numbers | BSD `<sys/ioccom.h>`: `IOC_OUT 0x40000000`, `IOC_IN 0x80000000`, 13-bit size | `_IOW` requests have a different number (table in section 4); `_IOWR` and all sizes used here are the same. Checked in the built binaries: the Darwin driver contains `0x80186440`, `0x80086409`, `0x80086449`, `0x8028644a` |
| ioctl size limit | XNU copies `IOCPARM_LEN(cmd)` bytes in and out itself, at most 8191 | largest argument used is 64 bytes (`DRM_IOCTL_VERSION`) |
| Nested pointers | the main argument reaches the driver as a kernel copy; pointers inside it (`drm_version.name`, `get_params.pointer`, `vm_bind.userptr`, `submit.syncs`/`cmdbuf`, `syncobj_wait.handles`) are real user addresses | the kernel side must copy them from the calling task |
| mmap | `mmap(NULL, len, PROT_READ\|PROT_WRITE, MAP_SHARED, drm_fd, offset)`; `offset` and `len` must be multiples of 16 KiB; the kernel asks the kext for the physical pages once, at mmap time, and maps them through a device pager | `MAP_PRIVATE` on the node fails (`ENODEV`); pages are fixed for the life of the mapping. Mesa's asahi rounds every BO to 16 KiB already (`AIL_PAGESIZE`) |
| `poll`/`select` on the DRM fd | readiness only, no wakeup (`sf_dri_poll`) | not used by asahi off-screen |
| syncobj | DRM core `drm_syncobj.c` is in SiliconForest's DRM source list; all access is by ioctl on the DRM fd | none expected |
| sync files | `sf_sync_file.c`: fds are `sf_lfile` objects; merge and file-info ioctls exist; `poll()` returns `POLLNVAL`, `select` never wakes; cannot be sent over a socket | the off-screen path creates one sync file per `glFinish` and hands it straight back to the DRM fd (section 4); it never polls it |
| dma-buf / PRIME | export/import exist; dma-buf `mmap` and the sync-file ioctls do not | not used off-screen (0 PRIME calls in the trace) |
| `memfd`/shm | no `memfd_create` (configure: NO); Mesa falls back to `mkstemp`-style anonymous files | asahi off-screen creates none |

### What Mesa's asahi needs from the OS beyond DRM

Configure results for the Darwin target and the code paths they select (all compile and link;
none has run):

- Threads: pthreads; `u_cnd_monotonic` uses `pthread_cond_timedwait_relative_np`. No futex:
  `simple_mtx` falls back to `mtx_t`. The driver starts a BO-cache trim thread and Mesa its
  shader-cache/util queues.
- Time: `clock_gettime(CLOCK_MONOTONIC)` (see risk 5).
- Shader cache identity: this Mesa has a Mach-O path in `src/util/build_id.c` (`LC_UUID` found
  through `dladdr`); `dl_iterate_phdr` is absent and not needed. The built
  `libgallium-26.3.0-devel.dylib` carries an `LC_UUID`.
- CPU detection: `sysctlbyname` path for Apple; memory size via `host_statistics64`.
- `os_same_file_description`: needs patch 0003 (no `kcmp`).
- Not available and not needed: `memfd_create`, `posix_fallocate`, `getrandom`,
  `pthread_setaffinity_np`, `reallocarray`, `secure_getenv` (Mesa has fallbacks).
- No LLVM in the target build: asahi compiles NIR to AGX itself, and its OpenCL C library
  (`libagx`) is compiled at build time by `mesa_clc`, `vtn_bindgen2` and `asahi_clc`, which
  `mesa-native.nix` builds for the build machine.
- Imports of the built driver are all satisfied by libSystem, libc++/libc++abi and libdrm
  (linked without `-undefined dynamic_lookup`).

## 2. Build

```
export PATH=$HOME/.nix-profile/bin:$PATH
F="--extra-experimental-features nix-command --extra-experimental-features flakes"
cd integrations/siliconforest/m1-mesa-asahi
nix-build $F mesa-native.nix --no-out-link              # host tools + Linux asahi + drm-shim
nix-build $F mesa-darwin.nix --no-out-link              # arm64 Darwin Mesa
nix-build $F agx-offscreen-test.nix --no-out-link       # arm64 Darwin test bundle
nix-build $F agx-offscreen-test-linux.nix --no-out-link # builder-side test + ioctl tracer
```

| Component | Result | Store path (this session) |
|---|---|---|
| libdrm (SiliconForest's port, flake `libdrm-dri-arm64`, unmodified) | builds, links | `/nix/store/nr6pc63d1v4air0aajnaz029bk775kp4-puredarwin-libdrm-amdgpu-2.4.133` |
| Host tools `mesa_clc`, `vtn_bindgen2`, `asahi_clc`; Linux x86-64 asahi + llvmpipe + softpipe, surfaceless EGL, `libasahi_noop_drm_shim.so` | builds, links, runs on the builder | `/nix/store/3x55f3rsqbhnx90a4fxk89vpix187c6z-darwin-lab-mesa-asahi-native-26.3.0-devel-147a62f6190` |
| `src/asahi` (isa, genxml, libagx, compiler, clc, layout, lib) for arm64 Darwin | builds | part of the next row |
| Gallium `asahi` + `softpipe`, winsys, EGL (surfaceless), GLES2/3: `libgallium-26.3.0-devel.dylib` (31 MB), `libEGL.1.dylib`, `libGLESv2.2.dylib` | builds, links (1203 ninja steps, 5 patches) | `/nix/store/xz1bzz7vr50xvqygzksk9zvx1s7r885a-darwin-lab-mesa-asahi-arm64-darwin-26.3.0-devel-147a62f6190` |
| `agx-offscreen-test` and `agx-panel-demo` Mach-O arm64 + relocated library bundle | builds, links | `/nix/store/ysmyxlscs86id9inwvhx0jpvwl8ygwsn-darwin-lab-agx-offscreen-test-arm64-darwin` |
| Builder-side test, panel demo, tracer, `run-shim`, `run-sw` | builds, runs | `/nix/store/hjb277ycmm5zqg0i568ks7zhfiynqzkl-darwin-lab-agx-offscreen-test-linux` |
| GBM, Vulkan (honeykrisp), Wayland/X11 platforms | not attempted (`-Dgbm=disabled`, no Vulkan drivers, `-Dplatforms=`) | |

The Darwin test is dynamically linked; that is how SiliconForest's own Mesa is used
(`sfgltri`), and Mesa no longer `dlopen`s per-driver modules: `libEGL` links `libgallium`
directly. The bundle keeps clear of the image's `/usr/lib` Mesa:

```
usr/local/bin/agx-offscreen-test
usr/local/bin/agx-panel-demo           # section 6; SFGOP and its IOKit linked in statically
usr/local/lib/mesa-asahi/{libEGL.1,libGLESv2.2,libgallium-26.3.0-devel,libdrm.2}.dylib
LINKAGE.txt        # otool -L of all six after relocation
```

It also needs `/usr/lib/libSystem.B.dylib`, `libc++.1.dylib`, `libc++abi.dylib` and
`libIOKitCF.dylib` (the last through libdrm) from the image; whether the M1 root has
`libIOKitCF.dylib` was not checked.

### Patches (`patches/`, applied by `mesa-darwin.nix`; the native build uses none)

| File | Lines | Reason |
|---|---|---|
| `0001-meson-darwin-is-a-drm-system.patch` | 21 | Darwin counts as a KMS/DRM system and uses the `drm` DRI platform instead of XQuartz's `apple` |
| `0002-asahi-no-macos-iokit-wrap-tooling.patch` | 12 | keep `src/asahi` off its macOS reverse-engineering branch (`wrap.c`, Apple's AGX user client) when an IOKit framework is found |
| `0003-os-file-same-file-via-F_SF_SAMEFILE.patch` | 14 | `os_same_file_description` through SiliconForest's fcntl; there is no `kcmp` |
| `0004-u-screen-query-DRM_CAP_PRIME-on-darwin.patch` | 11 | ask the kernel for `DRM_CAP_PRIME` on Darwin as on Linux, so the call sequence matches the Linux reference |
| `0005-asahi-ioc-field-accessors-for-bsd-ioccom.patch` | 20 | `_IOC_DIR/_IOC_TYPE/_IOC_NR/_IOC_SIZE` for BSD `ioccom.h`; `agx_device.c` re-sizes `QUEUE_CREATE` with them |

Not a source patch: `compat/sys/sysmacros.h` (16 lines) on the include path, for
`src/loader/loader.c`. Patches 0001, 0003 and 0004 are the same edits SiliconForest's `mesa.nix`
makes with `substituteInPlace` for its other DRM drivers, here as patch files against this
source.

## 3. Test program: `test/agx-offscreen-test.c`

One C file, the same source on Linux and Darwin (libdrm, EGL, GLESv2).

1. Finds the render node whose `drmGetVersion` name is `asahi` (or `--node`), prints name,
   version, date, description; issues `DRM_IOCTL_ASAHI_GET_PARAMS` group 0 asking for 616 bytes
   and prints every field under its uAPI name, core masks, and the feature bits by name.
2. Surfaceless EGL, GLES2 context, 256x256 RGBA8 texture as FBO colour attachment.
3. Frame A: clear to (0.2, 0.4, 0.6, 1.0), `glFinish`, `glReadPixels`.
4. Frame B: same clear, a gradient triangle (red/green/blue corners) on the left half, a quad
   textured from a 4x4 `GL_NEAREST` texture on the right half, `glFinish`, `glReadPixels`.
5. Prints SHA-256 of both frames' RGBA bytes and the sampled pixels; exit 0 pass, 1 check
   failed, 2 could not set up.

### Pass criterion

| Check | Kind | Why it is valid across drivers |
|---|---|---|
| Frame A equals 65536 x (51,102,153,255); hash `ec9ff14546d41e0cf8f3856592a16639cceb74d293fb165a18806d3728cae3f7` | exact | the clear values are exactly representable in unorm8, no rasterisation involved |
| Frame B: 4 background points | exact | untouched by geometry |
| Frame B: 5 points inside the triangle against the barycentric colour computed in the test | +/-3 per channel | interpolation precision differs between rasterisers |
| Frame B: centre of each of the 16 texel blocks of the quad | +/-1 | nearest sampling far from texel edges |
| Frame B: 27648 +/- 600 non-background pixels | range | edge-rule differences only move edges |
| Frame B hash equals `--expect-b` | exact | only against the **same driver**: the hash the same source prints under Linux on this M1 |
| `--require-renderer "Apple M1"` | guard | catches a silent fall-back to softpipe; a renderer string alone is not evidence |

Software renderers are not a pixel reference for frame B's hash on the GPU. For what it is
worth, llvmpipe and softpipe on the builder both pass every check and both give frame B
`90e2f4ba629c368f5273790c69012fb07ff8c368928d295a4dd50d6007c8374d`. Ticket G5's wording "pixel
hash equals the software-renderer reference" is therefore only safe for frame A; for frame B
use the tolerance checks plus the Linux asahi hash.

### The Linux reference (for the lead; run on the M1's Linux, mesa-aurora 26.3.0 asahi installed)

Copy `test/agx-offscreen-test.c` there, then:

```
cc -O2 -o agx-offscreen-test agx-offscreen-test.c $(pkg-config --cflags --libs egl glesv2 libdrm)
./agx-offscreen-test --require-renderer "Apple M1" --dump-b frame-b-linux.rgba | tee agx-offscreen-linux.txt
```

(without the `.pc` files: `-I/usr/include/libdrm -lEGL -lGLESv2 -ldrm`). Keep the whole output:
the `drm version` and `params` lines are what F14's `GET_PARAMS` must reproduce, and the
`frame B ... sha256=` value is the `--expect-b` argument on Darwin. An ioctl log from the real
kernel, to compare with section 4:

```
strace -f -e trace=ioctl,mmap -o agx-offscreen-linux.strace ./agx-offscreen-test
```

### On SiliconForest (once F14 exists)

Copy the bundle's `usr/local` into the root, then:

```
/usr/local/bin/agx-offscreen-test --require-renderer "Apple M1" --expect-b <frame B sha256 from Linux>
MESA_SHADER_CACHE_DISABLE=1 ...            # first runs: one variable fewer
LIBGL_ALWAYS_SOFTWARE=1 /usr/local/bin/agx-offscreen-test --no-drm   # softpipe: tests EGL/libSystem without the GPU
```

The softpipe line needs no kernel driver and can be tried today; it has not been.

### On the builder (no GPU)

```
T=$(nix-build $F agx-offscreen-test-linux.nix --no-out-link)
$T/bin/run-sw                 # llvmpipe: PASS
$T/bin/run-shim trace.txt     # asahi on the drm-shim: FAIL by design (all pixels 0), writes the ioctl log
python3 trace/summarize.py trace.txt
```

## 4. For SF-F F14: what the off-screen test asks of the kernel

Source: `trace/shim-g13g-trace.txt` (184 lines, every ioctl and device mmap in order) and
`trace/shim-g13g-summary.txt`, recorded with `trace/ioctl-trace.c` from the Linux build of this
Mesa running the test on `libasahi_noop_drm_shim.so` with `ASAHI_SHIM_CHIP=g13g`. The shim is
not the kernel driver: it reports `features=0`, completes every submission at once and returns
no fd for a sync-file export. The tracer supplies a placeholder fd there so that the rest of
that path is recorded. The lines marked (code) below come from reading the driver, not from
the trace.

### Requests and their numbers

| Request | Linux | Darwin | Arg bytes | Calls |
|---|---|---|---|---|
| `DRM_IOCTL_VERSION` | `0xc0406400` | same | 64 | 20 (2 test, 18 EGL/loader) |
| `DRM_IOCTL_GEM_CLOSE` | `0x40086409` | `0x80086409` | 8 | 25 |
| `DRM_IOCTL_GET_CAP` (`DRM_CAP_PRIME` = 5) | `0xc010640c` | same | 16 | 1 |
| `DRM_IOCTL_SYNCOBJ_CREATE` | `0xc00864bf` | same | 8 | 10 (one with `CREATE_SIGNALED`) |
| `DRM_IOCTL_SYNCOBJ_DESTROY` | `0xc00864c0` | same | 8 | 10 |
| `DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD` (`EXPORT_SYNC_FILE`) | `0xc01864c1` | same | 24 | 2 |
| `DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE` (`IMPORT_SYNC_FILE`) | `0xc01864c2` | same | 24 | 2 |
| `DRM_IOCTL_SYNCOBJ_WAIT` | `0xc02864c3` | same | 40 | 6 |
| `DRM_IOCTL_ASAHI_GET_PARAMS` | `0x40186440` | `0x80186440` | 24 | 2 (616 bytes from the test, 600 from Mesa) |
| `DRM_IOCTL_ASAHI_VM_CREATE` | `0xc0186442` | same | 24 | 1 |
| `DRM_IOCTL_ASAHI_VM_BIND` | `0x40186444` | `0x80186444` | 24 | 28 |
| `DRM_IOCTL_ASAHI_GEM_CREATE` | `0xc0186445` | same | 24 | 25 |
| `DRM_IOCTL_ASAHI_GEM_MMAP_OFFSET` | `0xc0106446` | same | 16 | 22 |
| `DRM_IOCTL_ASAHI_GEM_BIND_OBJECT` | `0xc0286447` | same | 40 | 2 |
| `DRM_IOCTL_ASAHI_QUEUE_CREATE` | `0xc0186448` (**24** bytes, struct is 40) | same | 24 | 1 |
| `DRM_IOCTL_ASAHI_QUEUE_DESTROY` | `0x40086449` | `0x80086449` | 8 | 1 |
| `DRM_IOCTL_ASAHI_SUBMIT` | `0x4028644a` | `0x8028644a` | 40 | 4 |
| `mmap` of the device fd | | | | 22, all `addr=NULL`, `MAP_SHARED`, read+write, 16 KiB to 1 MiB |

Not called by this test: `ASAHI_VM_DESTROY`, `ASAHI_GET_TIME`, `ASAHI_GEM_MADVISE`, any PRIME
or dma-buf ioctl, `SYNCOBJ_TIMELINE_WAIT`/`QUERY`/`TRANSFER`/`RESET`/`SIGNAL`, `poll`.
The VM and the remaining objects are released by closing the fd.

### Call order

1. Test: `open(renderD*)`, `VERSION` x2 (lengths, then strings), `GET_PARAMS` group 0, `close`.
2. EGL initialise: device enumeration through libdrm (on Darwin `opendir(/dev/dri)`, `stat`,
   `sysctl kern.sf_dri.*`; on Linux sysfs), `VERSION` x16 on two short-lived fds.
3. Screen create on a new fd: `VERSION` x2, `GET_PARAMS` group 0 (600), `VM_CREATE`
   (`kernel_start`/`kernel_end` = top 32 GiB below `vm_end`), then three fixed-address objects,
   each `GEM_CREATE(VM_PRIVATE)` + `VM_BIND` x2, two of them also `GEM_MMAP_OFFSET` + `mmap`:
   zero page at GPU VA `0x100000000` (read-only bind, 64 KiB), scratch page at `0x100010000`,
   printf buffer at `0x1000000000`. Then `SYNCOBJ_CREATE` (the screen's timeline syncobj),
   `GET_CAP(PRIME)`.
4. Context create: BO create/bind/map, `QUEUE_CREATE` (flags 0, priority 1, `usc_exec_base`
   set, 24-byte form), a shared object `GEM_CREATE(WRITEBACK, vm_id 0)` + `VM_BIND` +
   `GEM_BIND_OBJECT(op BIND, usage TIMESTAMPS)`, `SYNCOBJ_CREATE` x2 (one `CREATE_SIGNALED`).
5. Per frame (twice):
   - more `GEM_CREATE` + `VM_BIND` + `GEM_MMAP_OFFSET` + `mmap` (over the whole run: 21
     objects `VM_PRIVATE`, 3 `VM_PRIVATE|WRITEBACK`, 1 shared `WRITEBACK`);
   - `SYNCOBJ_CREATE`, `SUBMIT` with commands `[SET_FRAGMENT_ATTACHMENTS (24 bytes),
     RENDER (240 bytes)]`, 0 in-syncs, 3 out-syncs: a binary syncobj, the screen syncobj as a
     **timeline point**, a second binary syncobj; `SYNCOBJ_DESTROY`;
     `SYNCOBJ_WAIT(timeout 0)` as a poll;
   - `glFinish`: `SYNCOBJ_HANDLE_TO_FD(EXPORT_SYNC_FILE)`, `SYNCOBJ_CREATE`,
     `SYNCOBJ_FD_TO_HANDLE(IMPORT_SYNC_FILE)`, `close(sync fd)` (code),
     `SYNCOBJ_WAIT(absolute timeout INT64_MAX, WAIT_ALL)`, `SYNCOBJ_DESTROY`;
   - `glReadPixels`: `SYNCOBJ_CREATE`, `SUBMIT` with `[COMPUTE (128 bytes)]`, 3 out-syncs,
     `SYNCOBJ_DESTROY`, `SYNCOBJ_WAIT(timeout 0)`; on a real kernel this poll is followed by a
     blocking `SYNCOBJ_WAIT(INT64_MAX)` when the job has not finished (code:
     `agx_sync_batch`), then the CPU reads the result through an existing mapping.
6. Teardown: `SYNCOBJ_DESTROY` x3, `GEM_BIND_OBJECT(op UNBIND)`, `GEM_CLOSE`, `QUEUE_DESTROY`,
   `SYNCOBJ_DESTROY`, `GEM_CLOSE` x24, `close`.

All 28 `VM_BIND`s carry one op of 32 bytes: 27 with flags `READ|WRITE`, 1 with `READ`; no
`UNBIND`, no `SINGLE_PAGE`. Every frame, including the clear-only one, submits one render
command and then, from `glReadPixels`, one compute command.

If the real `GET_PARAMS` sets `DRM_ASAHI_FEATURE_VM_STATUS`, Mesa also calls `GET_PARAMS`
group 1 (code: `agx_drm_vm_status`); the shim sets no feature bits, so the trace does not
show it. `GEM_MADVISE` can appear from the BO cache on longer runs (code).

## 5. Risks, most serious first

1. **ioctl dispatch by BSD number and by size.** Five of the requests above have a different
   number on Darwin, and `QUEUE_CREATE` arrives 16 bytes short of the kernel's struct (as it
   does on Linux for a G13). The kernel side must dispatch on the request's number field with
   BSD direction bits, zero-extend a short argument to the driver's struct, and not write past
   the caller's size. A Rust ioctl table built with Linux's `_IOW` values will not match
   `0x80...` requests.
2. **Sync objects carry the whole completion path.** Needed: binary syncobjs incl.
   `CREATE_SIGNALED`; a timeline point as a `SUBMIT` out-sync (fence chain); `SYNCOBJ_WAIT`
   that really blocks and is woken by job completion, with `WAIT_ALL`, timeout 0 and
   `INT64_MAX`; sync-file export and re-import per `glFinish` (the fd is a `sf_lfile`; it is
   only passed back by ioctl, so the missing `poll`/kqueue support does not matter here). If
   the export fails Mesa prints `export failed` and `glFinish` returns **without waiting**;
   the following readback still waits on its own batch.
3. **GEM mmap pins.** SiliconForest's mmap hook hands XNU physical page numbers once. Every page
   of the object must be populated at `mmap` time and stay put until the mapping goes
   (release callback), including for objects the GPU writes and the CPU then reads (the
   readback target, the timestamp and printf buffers). Offsets from
   `GEM_MMAP_OFFSET` must be 16 KiB multiples. `MAP_FIXED` on this path is unused by the test
   (all 22 mmaps have `addr=NULL`) and remains untested.
4. **`GET_PARAMS` must equal Linux on this M1**, field for field: Mesa derives its GPU VA
   layout from `vm_start`/`vm_end`/`vm_kernel_min_size` (and binds at fixed GPU VAs
   `0x100000000`, `0x100010000`, `0x1000000000`), picks the shader set from
   generation/variant/`chip_id`, sizes submissions from `max_commands_per_submission`, and
   enables paths from `features`. `drmGetVersion` must say `asahi` with Linux's
   major.minor (`0.0.0` in `drivers/gpu/drm/asahi/driver.rs`; Mesa switches a render-command
   field on at 0.2). Mesa cannot detect a short reply: the request is write-only, it assumes
   it got the 600 bytes it asked for, and fields the kernel did not write read as zero.
5. **Two monotonic clocks.** `SYNCOBJ_WAIT` timeouts are absolute `CLOCK_MONOTONIC`
   nanoseconds. User space computes it as wall time minus boot time
   (libSystem `clock_gettime`); LinuxKPI's `ktime_get` comes from `sfos_uptime_us`. This test
   only uses 0 and `INT64_MAX`, so it is unaffected (provided `INT64_MAX` is clamped, not
   overflowed, on the way to a jiffies timeout); any finite wait
   (`glClientWaitSync`, a compositor frame) depends on the two agreeing.
6. **Nested user pointers and W-only requests.** `GET_PARAMS`, `VM_BIND` and `SUBMIT` are
   `_IOW`: XNU copies nothing back, and the results (`GET_PARAMS` data) or inputs (bind ops,
   sync array, command buffer) travel through pointers inside the argument, which are real
   user addresses, not addresses in SFDRMDri's kernel copy of the argument.
7. **Platform-device identity.** libdrm classifies the node from the `kern.sf_dri.devices`
   line. The asahi DRM device must be a platform device with an `of_node` whose `full_name`
   and `compatible` are set, or the line is emitted in PCI form and Mesa's loader takes the
   PCI-id path. The node gets `renderD(128 + provider index)`.
8. **Compute is required.** The readback after each frame is a compute submission, so F15
   cannot pass with the render path alone.
9. **Unverified on the target:** softpipe and EGL start-up on SiliconForest's libSystem, the
   shader disk cache (needs a writable cache directory; disable it at first), thread creation
   from the driver, and the presence of `libIOKitCF.dylib` in the M1 root.
10. **Diagnostics.** Mesa's asahi prints errors with glibc's `%m`; Darwin's `printf` does not
    expand it, so failed-ioctl messages lose the errno text. `agx_fallback_wait` (failure
    paths only) calls `poll` on a sync file, which returns `POLLNVAL` here.
11. **Pin drift.** `sources/gpu-plan/mesa` (`a1969111`) is not the commit built here or
    running on the M1; anyone reading that tree is reading a different driver.

## 6. On-screen demo and benchmark: `test/agx-panel-demo.c`

GLES3 on surfaceless EGL, rendering into FBOs at panel resolution, shown on the built-in panel
through the same CPU path the compositor uses: `SFGOPOpen` (64-byte `SFGOPFramebuffer`,
2560x1600, stride 10240, x2r10g10b10), stores into the mapping, no `SFGOPPresent` (that call is
for the VirtIO service only). The mapping is read exactly once, to save the console image; the
image is put back after every present run and at exit, and the program prints only while it is
back, as `wm-darwin`'s benchmark does. `SIGINT`/`SIGTERM` stop the run and still restore. If
SFGOP does not open, or is not a directly mapped x2r10g10b10 framebuffer, the present step
writes to a heap buffer of the same layout and says `sink=heap`.

| Scene | Content |
|---|---|
| `plasma` | one full-screen triangle, procedural fragment shader (four `sin`, one `sqrt` per pixel) |
| `quads` | 10000 textured, alpha-blended quads from a 256x256 atlas, one indexed draw call; about 17 layers of overdraw at 2560x1600 |
| `mesh` | nine tori (16384 triangles each), per-pixel diffuse + specular lighting, 24-bit depth buffer |

| Mode | One frame is |
|---|---|
| `render` | draw, `glFinish` |
| `readback` | draw, `glReadPixels` of the whole target |
| `present` | draw, `glReadPixels`, rows into the panel mapping |

| Path | Target and present step |
|---|---|
| `rgba8` | RGBA8 target in GL orientation. Present converts each pixel to x2r10g10b10 and reverses the row order on the CPU, a row at a time through a 10 KiB buffer (the compositor's shape of work) |
| `rgb10` | RGB10_A2 target; the shaders flip y and swap red and blue, so a `GL_RGBA`/`GL_UNSIGNED_INT_2_10_10_10_REV` readback is already the panel's word (`r<<20 \| g<<10 \| b`), top row first. Present is one 16 MB copy |
| `direct` | as `rgb10`, but `glReadPixels` writes straight into the panel mapping (present mode only): no second copy |

asahi supports the 10-bit path as far as the builder can tell: on the drm-shim the driver
reports `GL_IMPLEMENTATION_COLOR_READ_FORMAT/TYPE` = `GL_RGBA`/`GL_UNSIGNED_INT_2_10_10_10_REV`
for an RGB10_A2 target (`AIL_FMT(R10G10B10A2_UNORM, ...)` in `src/asahi/layout/formats.c`), and
all 21 runs complete without a GL error. The program asks at run time and drops the `rgb10` and
`direct` paths if the answer differs.

Each scene runs render/readback/present on `rgba8`, the same on `rgb10`, then present on
`direct`: 7 runs per scene, 21 in all. A run is `--frames` frames (default 30) after one
untimed warm-up frame, cut short after 2 s (never below 5 frames); `--budget` seconds (default
50) after start, remaining runs are skipped with an `AGXBENCH_SKIP` line. Frames depend on the
frame index only, never on the clock.

### Output

```
agx-panel-demo: GL_RENDERER=Apple M1 (G13G B1)
agx-panel-demo: GL_VERSION=OpenGL ES 3.2 Mesa 26.3.0-devel
agx-panel-demo: RGB10_A2 read format 0x1908 type 0x8368: 10-bit path on
agx-panel-demo: size=2560x1600 frames=30 budget_s=40 sink=panel stride=10240 setup_ms=...
AGXHASH scene=plasma target=rgba8 frame=7 size=2560x1600 sha256=<64 hex>
AGXHASH scene=plasma target=rgb10 frame=7 size=2560x1600 sha256=<64 hex>
AGXCHECK scene=plasma differ_px=0 of=4096000 max_diff=3 flat=0 result=ok
AGXBENCH scene=plasma mode=render frames=30 median_us=.. min_us=.. max_us=.. fps=.. path=rgba8 sink=none read_us=.. copy_us=0
AGXBENCH scene=plasma mode=readback frames=30 ... path=rgba8 sink=none read_us=.. copy_us=0
AGXBENCH scene=plasma mode=present frames=30 ... path=rgba8 sink=panel read_us=.. copy_us=..
AGXBENCH scene=plasma mode=render ... path=rgb10 ...
AGXBENCH scene=plasma mode=readback ... path=rgb10 ...
AGXBENCH scene=plasma mode=present ... path=rgb10 sink=panel ...
AGXBENCH scene=plasma mode=present ... path=direct sink=panel read_us=.. copy_us=0
  ... the same 10 lines for quads and for mesh ...
AGXBENCH_DONE status=ok skipped=0 seconds=..
```

The first eight fields of an `AGXBENCH` line are the requested ones; `path`, `sink`
(`panel`, `heap` or `none`), `read_us` (median time from the start of the frame to the end of
`glFinish`/`glReadPixels`) and `copy_us` (median of the CPU convert/copy that follows) are
appended. `fps` is 10^6 / `median_us`. Exit status 0 only for `status=ok`.

- `AGXHASH`: SHA-256 of frame 7 as read back, per scene and target. Comparable only between
  runs of the same driver at the same size (Linux asahi on this M1 against Darwin asahi).
- `AGXCHECK` needs no reference: it converts the `rgba8` frame exactly as the present step
  does and compares it with the `rgb10` frame, so the flip, the channel swap and the
  conversion are checked against each other; it fails if more than 2 % of pixels differ by
  more than 16/1023 in a channel, or if the frame is a single flat colour. On llvmpipe at
  640x400 the largest difference is 3, 13 and 3 (plasma, quads, mesh) and no pixel exceeds 16.

### Linux reference (for the lead, on the M1's Linux)

```
cc -O2 -o agx-panel-demo agx-panel-demo.c $(pkg-config --cflags --libs egl glesv2) -lm
./agx-panel-demo --require-renderer "Apple M1" --frames 30 | tee agx-panel-linux.txt
./agx-panel-demo --require-renderer "Apple M1" --frames 30 --no-present    # without the heap present runs
```

There is no panel access in the Linux build: `present` runs write to a heap buffer
(`sink=heap`), which measures readback plus conversion without the cost of the panel mapping.
`--dump prefix` writes frame 7 of each scene as `prefix-<scene>.ppm` for a look at the picture.
The six `AGXHASH` lines are the reference for Darwin.

### On SiliconForest

Bundle: `nix-build $F agx-offscreen-test.nix --no-out-link` (store path in section 2); the four
libraries and `agx-offscreen-test` are byte-identical to the bundle that passed on hardware,
only `usr/local/bin/agx-panel-demo` is new. Staging list, with `T` that store path:

```
$T/usr/local/bin/agx-panel-demo=/usr/local/bin/agx-panel-demo
$T/usr/local/bin/agx-offscreen-test=/usr/local/bin/agx-offscreen-test
$T/usr/local/lib/mesa-asahi/libEGL.1.dylib=/usr/local/lib/mesa-asahi/libEGL.1.dylib
$T/usr/local/lib/mesa-asahi/libGLESv2.2.dylib=/usr/local/lib/mesa-asahi/libGLESv2.2.dylib
$T/usr/local/lib/mesa-asahi/libgallium-26.3.0-devel.dylib=/usr/local/lib/mesa-asahi/libgallium-26.3.0-devel.dylib
$T/usr/local/lib/mesa-asahi/libdrm.2.dylib=/usr/local/lib/mesa-asahi/libdrm.2.dylib
```

`tools/agx_boot.sh` builds its own `PCIE_EXTRA_FILES` from this recipe and lists only
`agx-offscreen-test` and the libraries; it needs the first line above added (that file is
outside this directory). Guest script: `supervisor/darwin-agx-panel-demo.in`
(`AGX_TEMPLATE=...`), prefix `DL42_`, runs
`agx-panel-demo --require-renderer "Apple M1" --frames 30 --budget 40 --hold-ms 250`
and prints `DL42_PANEL_RC=<rc> seconds=<n>`. `--hold-ms` leaves the last frame of each
present run on the panel for that long before the console comes back (9 runs, 2.25 s in all).
Expected wall time: at most 40 s of runs plus the run in progress (2 s cap plus its warm-up
frame), the holds and set-up; the 2 s cap bounds 21 runs to about 42 s even if every frame
were slow, so on a slow target the last runs are skipped rather than the script overrunning.

### What the demo adds to the kernel's workload (from the drm-shim trace, 5 frames per run)

Same request set as section 4 plus `DRM_IOCTL_ASAHI_GEM_MADVISE` (once, from the BO cache).
Objects of 16 MB (`0xfa0000`, `0xfe0000`) are created and mapped. Render submissions carry two
attachments (`SET_FRAGMENT_ATTACHMENTS:48`, colour and depth); every `glReadPixels` is one
`COMPUTE` submission; every `glFinish` is the sync-file export/import/wait sequence. A full
default run is about 1100 submissions (651 render, about 470 compute) in under a minute.

### Risks of the demo

1. **The panel is left showing a frame if the process dies.** A crash or `SIGKILL` between a
   present and the restore leaves the last GPU frame up; the text console repaints as it
   scrolls. The serial log is unaffected.
2. **Static IOKit next to libIOKitCF.** The demo links SFGOP's `libIOKit.a` members into the
   executable while `libdrm.2.dylib` (loaded through `libEGL`) depends on
   `/usr/lib/libIOKitCF.dylib`, which exports functions of the same names with other
   signatures. Two-level namespace binding keeps them apart, and libdrm's `/dev/dri` path makes
   no IOKit calls; this combination has not run.
3. **Direct readback into a write-combined mapping** relies on Mesa only writing to the
   destination. It is the path most likely to be slow or wrong on the target; it is last in
   each scene, and the other two present paths do not depend on it.
4. **Readback cost is unknown.** On Linux `glReadPixels` of 16 MB from this driver is a
   compute job plus a CPU copy out of a GPU buffer mapping; if SiliconForest maps that buffer
   uncached or write-combined the copy is far slower than on Linux. `read_us` minus the
   `render` median shows it.
5. **GLES3 and 16 MB objects are new on the target**: the hardware-proven test used a GLES2
   context and a 256x256 target.
6. **Timer resolution**: libSystem's `CLOCK_MONOTONIC` is derived from `gettimeofday`
   (microseconds, and it moves if wall time is set during the run).

## 7. Using the GPU from the compositor (on paper)

State today: `wm-darwin` is Smithay's Wayland front end with a tiny-skia CPU renderer, `wl_shm`
clients only, SFGOP stores as the only output. Direct scanout is out of scope until the display
controller driver (SF-H); every option below ends in a readback and a copy to the panel.

**Option 1: GL renderer in the compositor, FBO plus readback (what section 6 measures).**

- Mesa: nothing new. Surfaceless EGL, GLES2/3 and asahi are in the bundle.
- Kernel: nothing new beyond what the demo already uses.
- Compositor: a second implementation of its renderer trait on GLES. Client buffers stay
  `wl_shm` and are uploaded with `glTexSubImage2D` for damaged regions; the scene is drawn
  into an RGB10_A2 FBO with the flip and channel swap of the `rgb10` path; output is
  `glReadPixels` of the damaged rows into the SFGOP mapping. Smithay's GLES renderer expects
  an EGL display from a native display type; a surfaceless one has to be supplied (not
  checked against Smithay 0.7's API).
- Cost per full frame is the demo's `present` line for `rgb10` or `direct`; with damage
  tracking only the damaged rows are read back. If that line is not clearly below the CPU
  compositor's 24 ms write plus 9 ms convert, this option only pays for scenes the CPU cannot
  draw in time (blur, scaling, many surfaces), not for a terminal.
- Size: 2 to 3 weeks (ticket G6). No dependency on G3/G4.

**Option 2: zero-copy clients (GPU clients hand dma-bufs to the compositor).** Builds on
option 1; it removes the client-side readback and the `wl_shm` upload, not the final copy.

| Piece | State | Work |
|---|---|---|
| Mesa GBM (`-Dgbm=enabled`) | builds and links for Darwin with the five patches here and nothing else (`libgbm.1.dylib`, `dri_gbm.dylib`; tried in a scratch tree, not in a recipe) | a recipe flag; where `libgbm` looks for its backend at run time is untested |
| Mesa EGL Wayland platform for clients (`-Dplatforms=wayland`) | not attempted; needs the flake's `wayland-arm64`, `wayland-protocols`, a host `wayland-scanner`, and the `ppoll` shim SiliconForest's `mesa.nix` adds for `loader_wayland_helper.c` | 2 to 3 days |
| `EGL_EXT_image_dma_buf_import` (+ `_modifiers`) in the compositor's EGL | Mesa enables it when the kernel answers `DRM_CAP_PRIME` (patch 0004 makes Darwin ask) and the driver can import; asahi can | check the extension string on the target |
| Compositor protocol: `linux-dmabuf` v4 with feedback (Smithay has the state objects); legacy `wl_drm` is not needed by current Mesa clients | missing | about 1 week with the import path |
| Kernel: PRIME handle to fd and fd to handle across two DRM files | DRM core `drm_prime.c` + `sf_dma_buf.c`; unexercised by asahi so far (0 PRIME calls in both traces) | test first |
| Kernel: dma-buf fds over a Unix socket (`SCM_RIGHTS`) | missing: the file type is not sendable (ticket G3) | 1 week with the item below |
| Kernel: `poll`/kqueue on sync files and DRM fds | missing or level-only (G3) | Wayland clients block on these |
| Kernel: `DMA_BUF_IOCTL_EXPORT_SYNC_FILE` / `IMPORT_SYNC_FILE` | missing (G4). asahi calls both for every shared buffer on every submission; when they fail it falls back to waiting on the CPU, so this is a performance item, not a correctness one | 1 week |
| Kernel: dma-buf `mmap` | missing (G4); the GL import path does not need it, CPU fallbacks and screenshots do | with the above |
| Device identity for clients | `linux-dmabuf` feedback sends a `dev_t`; open DRM nodes here are clones with a different minor than the node. libdrm's patch compares the low byte, Mesa calls `drmGetDeviceFromDevId` | test with the first client |

Order: run section 6 and read the numbers; option 1; then G3 (fd passing and wake-ups), the
Mesa GBM/Wayland build and `linux-dmabuf` in the compositor, with G4 for speed; Vulkan and
scanout later. Option 2 is about 4 to 5 weeks on top of option 1, most of it kernel file
plumbing that does not need the GPU driver.

## 8. Chasing a frame that differs between Linux and Darwin (demo diagnostics)

Case: on the M1, `AGXHASH scene=mesh target=rgb10` differs between Darwin and Linux while the
other five fixed frames are equal (`reports/sprint-010/gpu-panel-demo-001.md`). The diagnostics
below are in `agx-panel-demo`; on the builder they have run on llvmpipe, softpipe and the
drm-shim only. Coordinates are in readback-buffer space (x from the left, y = buffer row: the
bottom-up GL row for `rgba8`, the top-down panel row for `rgb10`).

| Option | Lines | What it answers |
|---|---|---|
| `--tiles` (passive) | `AGXTILES` x10 per frame: 16x10 grid of 32-bit hashes of 160x160 tiles; `AGXSTAT`: byte-lane sums, pixels equal to the first pixel (background); `AGXDIFFHIST`: histogram of the per-pixel `rgba8`-versus-`rgb10` difference | which tiles differ; whether a difference is a few pixels or a region |
| `--fp-out F` / `--fp-ref F` (passive) | reference side writes hashes of every 16x16 block (about 150 KiB per frame at 2560x1600); the other side prints `AGXFPDIFF ... blocks_differ=N bbox=... blocks=x,y;...` and `AGXPIX` (16 rows of 16 raw 32-bit words) for the first three differing blocks | the exact blocks and the pixels in them, in one boot |
| `--pixels scene:target:x:y` | `AGXPIX` for that 16x16 block | the same block on the other system |
| `AGXCHECK` (always) now ends with `worst_at=x,y worst_rgba8=r,g,b worst_rgb10=r,g,b` | | where the one pixel with `max_diff=370` is, and its two values |
| `--repeat-hash N` | `AGXREPEAT ... differing_runs=K stable=yes/no`, and for the first differing run `AGXREPEATDIFF` with block count, bounding box and both versions of the first block | nondeterminism against a deterministic difference |
| `--variants` | `AGXVAR variant=finish-before-read`, `AGXVAR variant=own-depth-buffer`: the `rgb10` frame again, `same_as_base=yes/no`; for the mesh, `AGXDEPTH after=rgba8/rgb10`: a picture of the depth buffer after the frame was read back, with its tile hashes | readback racing the render; the depth buffer shared by both targets; depth lost or altered across a store and load |
| `--fresh` | `AGXGEM round=1/2`: eight fresh 4 MiB GEM objects straight from the kernel (no Mesa), scanned for non-zero bytes, filled with `0xa5`, freed, and eight more scanned; `AGXFRESH`: never-written GL targets read back | whether the kernel hands out non-zeroed or recycled pages |
| `--no-bench`, `--diag` | | fixed frames and diagnostics only; `--diag` = `--tiles --repeat-hash 3 --variants` |

Passive options only hash frames the run reads anyway, so the run's `AGXHASH` values are those
of a plain run. The active ones add renders and allocations after both fixed frames of a
scene, which changes what later scenes see; the guest script therefore uses them in a second,
mesh-only process.

`AGXGEM` is a real detector: on the drm-shim, which recycles memory without clearing it, round
1 reports `zero` and round 2 `NOT-ZEROED` (33554432 of 33554432 bytes, value `0xa5`).
`AGXFRESH` is weaker in the zero direction: the driver may answer for an uninitialised target
without touching its memory, so only a non-zero count means something.

### Reference on Linux, then Darwin

```
./agx-panel-demo --require-renderer "Apple M1" --frames 30 --tiles --fp-out panel-linux.fp | tee panel-linux.txt
./agx-panel-demo --require-renderer "Apple M1" --no-present --no-bench --scenes mesh --diag --repeat-hash 5 --fresh \
    --fp-out panel-linux-mesh.fp | tee panel-linux-mesh.txt
```

Stage the two `.fp` files as `/usr/local/share/agx/panel-linux.fp` and
`/usr/local/share/agx/panel-linux-mesh.fp` (optional; without them the script still prints
tiles). `supervisor/darwin-agx-panel-demo.in` now runs the benchmark with `--tiles` and the
reference (`DL42_PANEL_RC`), then the mesh-only diagnostic process (`DL42_DIAG_BEGIN` ...
`DL42_DIAG_RC`). The benchmark took 17.8 s on the target; the additions are a few seconds.

### Reading the result

| Observation on Darwin | Points to |
|---|---|
| run 1 `AGXFPDIFF mesh rgb10`: a handful of blocks on torus outlines or where tori overlap | a depth-test outcome differing in single pixels: depth precision or depth store/load, not memory contents |
| the same, but whole 32x32-aligned squares or rows of them | a tile that was loaded or stored differently (partial render when the tiler's buffer fills, or stale tile memory) |
| `AGXREPEAT mesh rgb10 stable=no` | a race or uninitialised input; `AGXREPEATDIFF` shows where |
| run 2 (fresh process, mesh only) gives Linux's hash for its own run 2, while run 1 does not | dependence on what ran before: recycled buffers or driver/kernel state grown by the earlier scenes |
| `AGXVAR own-depth-buffer same_as_base=no` on one system only | the shared depth buffer: what the `rgba8` frame left in it reaches the `rgb10` frame |
| `AGXVAR finish-before-read same_as_base=no` | the readback does not wait for the render |
| `AGXDEPTH after=rgb10` hash differs from Linux while `after=rgba8` agrees | the depth buffer itself differs for the flipped frame; colour is a consequence |
| `AGXGEM ... NOT-ZEROED` | the kernel's GEM allocation; fix that first and re-run |

Not checked by anything here: `GEM_MADVISE`. A kernel that ignores it keeps buffer contents,
which Mesa allows for; one that discards pages and still reports them retained would hand
Mesa's cache zeroed buffers, which Mesa does not rely on either. It would not explain a
deterministic single-frame difference.

## 9. Zero-copy present: `agx-panel-demo --kms`

Not run on the target at the time of writing. Kernel-side findings go to `KERNEL-NEEDS.md`.

Route: each of three 2560x1600 buffers is a GEM object of the GPU driver, created with the
asahi `GEM_CREATE` ioctl on the render node exactly as `m1-dcp/test/dcp-flip-test.c
--prime-from auto` does, and exported as a PRIME fd. The fd is imported into the card node
(`ADDFB2` XRGB8888, pitch 10240) and into GL: `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)` with
fourcc ARGB8888 (XRGB8888 if refused), `DRM_FORMAT_MOD_LINEAR`, then
`glEGLImageTargetTexture2DOES` on a texture attached to an FBO with the demo's depth
renderbuffer. No Mesa change was needed for this: the Darwin build already contains the
import code (`dri2_create_image_dma_buf`, `dri2_from_dma_bufs`, `agx_resource_from_handle`),
the extension is advertised when the kernel answers `DRM_CAP_PRIME` (patch 0004), and on the
drm-shim the asahi driver lists the linear modifier (`0x0`) for AR24 and XR24.
`agx_resource_from_handle` accepts a linear import whose pitch is a multiple of 16.

Scenes are drawn y-flipped (buffer row 0 is the top scanline) and without the channel swap
(GL writes B,G,R,A bytes for this format, which is XRGB8888 to the display).

Per scene:

- `AGXKMSCHECK scene=S frame=7 gl_vs_cpu_map=equal|different blocks_differ=N
  kms_vs_render_path=equal|different blocks_differ=N of=16000 flat=0|1 sha256=... result=ok|FAIL`:
  the fixed frame rendered into a scanout buffer; `glReadPixels` of it against the CPU's view
  of the same memory through the render-node mapping, and against the same frame rendered
  into an ordinary GL-allocated RGBA8 target (alpha ignored). On a difference, `AGXKMSDIFF`
  gives the bounding box and `AGXPIX` both versions of the first differing 16x16 block. With
  `--fp-ref`, `AGXFPDIFF ... target=kms` compares with the reference's `rgba8flip` section.
- then `--kms-seconds` (default 5) of: draw frame N into a buffer that is neither on screen
  nor pending, `glFinish`, block in `read()` for the previous flip's event, atomic commit of
  `FB_ID` with `PAGE_FLIP_EVENT | NONBLOCK` (first commit: `ALLOW_MODESET`, blocking):
  `AGXKMS scene=S frames=N seconds=.. fps=.. render_ms_median=.. render_ms_p99=..
  flip_wait_ms_median=.. missed_vblanks=.. ebusy_retries=.. flips=.. interval_ms_min=..
  interval_ms_max=.. event_sequence_span=.. present=kms`. `missed_vblanks` counts intervals
  between flip-complete events above 25 ms; `event_sequence_span` is the kernel's own vblank
  count over the scene (flips - 1 when none was missed, if the driver counts).
- `AGXKMS_FAIL call="..." errno=N (...)` names the first call that failed; `AGXKMS_DONE
  status=ok|check-failed|gl-error|interrupted`.

`--kms-first`: one buffer, the fixed frame of the first scene, its check, one modeset commit,
its event (`AGXKMS_FIRST ... result=flipped`), exit. `--kms-noflip`: no card node; everything
else, including the dma-buf import into GL (for Linux, where a compositor owns the display).

```
# Linux reference on the M1 (libdrm is now needed to build the demo)
cc -O2 -o agx-panel-demo agx-panel-demo.c $(pkg-config --cflags --libs egl glesv2 libdrm) -lm
./agx-panel-demo --require-renderer "Apple M1" --kms-noflip --kms-seconds 2 --fp-out panel-linux-kms.fp | tee panel-linux-kms.txt

# Darwin, one command (collection with SFAgx and SFDcp)
DCP_TEST=kms [DCP_KMS_REF=panel-linux-kms.fp] tools/dcp_boot.sh <name> <kc-store-path>
```

`DCP_TEST=kms` stages `agx-panel-demo`, the four libraries and
`supervisor/darwin-agx-kms-demo.in` (markers `DL46_KMS_BEGIN_`, `DL46_FIRST_RC`,
`DL46_KMS_RC`, `DL46_RC`, `DL46_KMS_END_`) and adds `sfagx=1`, `sfdcp=2`. The script runs
`--kms-first --scenes plasma`, and the full `--kms` only if that passed.

Not handled: the demo does not restore the text console after the last flip; what the panel
shows after the card node is closed is the display driver's business.

## 10. GPU-rendered Wayland clients (ticket G7, client side)

Not run on the target. What the compositor must provide is in `WAYLAND-NEEDS.md`
(`zwp_linux_dmabuf_v1` version 3 with `modifier` events for XR24/AR24 linear is enough).

- `mesa-darwin-wayland.nix`: the same Mesa with `-Dplatforms=wayland` (EGL platforms
  `wayland surfaceless`), built against the flake's Darwin libwayland 1.25.0
  (`wayland-arm64`), its `wayland-protocols` 1.48 and nixpkgs' `wayland-scanner` on the build
  machine. `mesa-darwin.nix` is unchanged (same derivation as before; both now call
  `mesa-darwin-build.nix`). `libgallium` and `libEGL` differ between the two builds,
  `libGLESv2` does not.
- Five more patches, applied only to the Wayland build:

  | File | Lines | Reason |
  |---|---|---|
  | `0006-egl-wayland-open-render-node-without-feedback.patch` | 37 | with a linux-dmabuf v3 server (no feedback, and `wl_drm` is not built) Mesa has no device and falls back to software; on Darwin it now opens the first DRM device with a render node, found through libdrm's sysctl enumeration |
  | `0007-loader-wayland-ppoll-for-darwin.patch` | 31 | `ppoll` for Mesa's Wayland dispatch helper, as SiliconForest's own Mesa recipe shims it |
  | `0008-egl-pointer-check-without-mincore-on-darwin.patch` | 19 | `_eglPointerIsDereferenceable` used `mincore`, which on XNU succeeds for unmapped addresses; Mesa then took a `wl_egl_window`'s version field (3) for a pointer to an old-layout `wl_surface` and crashed in `wl_proxy_create_wrapper` (boot wl02). On 64-bit Darwin the check is now `address >= 4 GiB` (`__PAGEZERO`) |
  | `0009-report-why-a-window-buffer-could-not-be-exported.patch` | 118 | diagnostics only: `agx_bo_make_shared` names the step and errno when an export fails, and `create_wl_buffer` says why it made no `wl_buffer` (failed modifier query with errno, modifier not advertised with the compositor's list, failed fd query) |
  | `0010-asahi-export-wait-for-writer-when-fence-import-fails.patch` | 32 | on export, if the pending writer's fence cannot be attached to the dma-buf (`DMA_BUF_IOCTL_IMPORT_SYNC_FILE`), wait for the writer and export anyway, with a message, instead of failing the export; the submit path already falls back the same way |

- `test/agx-wl-demo.c` (`agx-wl-demo.nix`): xdg-shell toplevel, `wl_egl_window`,
  `eglCreateWindowSurface`, GLES3, the three scenes at 800x600 (or the size the compositor
  configures), one frame per `wl_surface.frame` callback with swap interval 0, `--seconds`
  (default 10) split over the scenes. It includes `agx-panel-demo.c` as source for the scenes.
  It fails unless it is a hardware client handing over dma-bufs: the compositor must advertise
  `zwp_linux_dmabuf_v1`, the renderer must not be a software one, and libwayland's own
  protocol log of the process (`WAYLAND_DEBUG`, captured in a temporary file) must show
  `create_immed` requests and no `wl_shm.create_pool`.

```
AGXWL_INFO globals wl_compositor=4 xdg_wm_base=1 zwp_linux_dmabuf_v1=3 wl_shm=1 wp_presentation=0
AGXWL_INFO GL_RENDERER="Apple M1 (G13G B1)" GL_VERSION="..." size=800x600 config_alpha=0 native_visual=XR24 software=0 egl_dma_buf_import=1
AGXWLCHECK scene=plasma frame=7 size=800x600 window_vs_fbo=equal|different differ_px=.. over8_px=.. max_diff=.. blocks_differ=.. flat=0 sha256=.. result=ok|FAIL
AGXWL scene=plasma frames=.. seconds=.. fps=.. render_ms_median=.. render_ms_p99=.. frame_interval_ms_median=.. frame_interval_ms_max=..
  ... quads, mesh ...
AGXWL_BUFFERS counted=1 dmabuf_create_immed=N shm_create_pool=0 surface_attach=.. buffer_release=.. callback_done=..
AGXWL_DONE status=ok gl_error=0x0
```

  `AGXWLCHECK` compares the fixed frame in the window's back buffer with the same frame in a
  framebuffer object; up to 0.1 % of pixels may differ by more than 8/255 (the window's
  framebuffer is y-inverted inside the driver; llvmpipe differs in a few pixels by 1).
  `AGXWL_FAIL call="..."` names what went wrong (no connection, no dmabuf global, no
  configure within 5 s, no frame callback within 2 s, software renderer, shm fallback).

- Bundle (`agx-wl-demo.nix`), in a prefix of its own because its `libEGL`/`libgallium` are not
  the ones of the other bundle (33 MB):

```
usr/local/bin/agx-wl-demo
usr/local/lib/mesa-asahi-wl/{libEGL.1,libGLESv2.2,libgallium-26.3.0-devel,libdrm.2,libwayland-client.0,libwayland-egl.1}.dylib
```

  Run inside the session: `WAYLAND_DISPLAY`/`XDG_RUNTIME_DIR` as for the other clients, then
  `agx-wl-demo --require-renderer "Apple M1"`. `/tmp` must be writable for the protocol log.

- Linux reference on the M1 (both source files in one directory):

```
X=$(pkg-config --variable=pkgdatadir wayland-protocols)/stable/xdg-shell/xdg-shell.xml
wayland-scanner client-header $X xdg-shell-client-protocol.h
wayland-scanner private-code $X xdg-shell-protocol.c
cc -O2 -I. -o agx-wl-demo agx-wl-demo.c xdg-shell-protocol.c \
   $(pkg-config --cflags --libs wayland-client wayland-egl egl glesv2 libdrm) -lm
./agx-wl-demo --require-renderer "Apple M1" | tee agx-wl-linux.txt
```

- Checked on the builder (`agx-wl-demo-linux.nix`, `run-weston-test`, nixpkgs Mesa 26.1.5 and
  a headless weston, software rendering): connection, xdg-shell configure, EGL window
  surface, frame-callback pacing (60 fps), the window-versus-FBO check, the protocol-log
  counting (`shm_create_pool=1`, `dmabuf_create_immed=0`), and that without
  `--allow-software` the demo refuses (`AGXWL_FAIL call="zwp_linux_dmabuf_v1"`). Not
  checkable here: anything with dma-bufs, patch 0006 (Darwin-only code), the Darwin
  libwayland at run time, and Mesa's own v3 path against a real server.

## 11. Boot wl01: the client died in EGL/Wayland start-up; what was established

- The staged files were intact: all seven files of the bundle, extracted from that boot's
  `ramdisk.img` with `debugfs`, have the SHA-256 of the store files.
- "Text page corruption detected for pid N" was not corruption. XNU prints it on the exit path
  of a crashing process (`bsd/kern/kern_exit.c`, `proc_prepareexit` →
  `revalidate_text_page` in `osfmk/vm/vm_fault.c`) when the code-signature hash of the 16 KiB
  page holding the pc, the page before it, or the page holding the caller's return address
  does not match. Every file both bundles ship had exactly one stale hash, that of its first
  4 KiB (the Mach-O header): the linker signs ad hoc, and `install_name_tool` then rewrites
  load commands in that page when the recipes relocate the libraries. Nothing enforces the
  hash, so everything runs; the message only says that the crash was within the first 32 KiB
  of some image or was called from the first 16 KiB of one. In this bundle that points at
  libwayland-client's core (`wl_proxy_*`, `wl_display_*`, `dispatch_event`,
  `wl_connection_*`, all between 0x4000 and 0x8000), `eglInitialize` itself (0x6d7c in
  libEGL), all of libwayland-egl, or the GL entry stubs of libGLESv2. Which one is not known.
- `tools/cs-rehash.py` recomputes the hash table of an ad-hoc signature in place (there is no
  `codesign` in the toolchain; an ad-hoc signature is only that table). `agx-wl-demo.nix` now
  runs it after relocation and checks the result, so from this bundle on the kernel message
  means what it says. The other bundle (`agx-offscreen-test.nix`) is left as it was proven,
  stale first-page hashes included.
- `agx-wl-demo` now reports for itself: `step:` lines before every call from
  `wl_display_connect` to the second `eglSwapBuffers`, and a signal handler that prints
  `AGXWL_CRASH signal=.. fault_addr=.. pc=.. <image>+0x<offset> last_step=".."`, one
  `AGXWL_CRASH_FRAME` per return address up the frame-pointer chain, and one
  `AGXWL_CRASH_IMAGE base=.. name=..` per loaded image. `<image>+0x<offset>` is the address
  `nm` prints for a dylib of the bundle (for the executable, minus 0x100000000).
  `--crash-test` faults on purpose; `--no-protocol-log` runs without `WAYLAND_DEBUG` and the
  stderr redirection, which removes libwayland's debug printing from the picture.

## Files

```
common.nix                     flake pin, Mesa source pin
mesa-native.nix                build-machine tools, Linux asahi + drm-shim
mesa-darwin.nix                arm64 Darwin Mesa (asahi, softpipe, surfaceless EGL, GLES2/3)
mesa-darwin-wayland.nix        the same with the Wayland EGL platform (both call mesa-darwin-build.nix)
agx-wl-demo.nix                arm64 Darwin Wayland client bundle
agx-wl-demo-linux.nix          builder-side build of it and a headless weston run
agx-offscreen-test.nix         arm64 Darwin bundle: both programs and the libraries
agx-offscreen-test-linux.nix   builder-side builds of both programs, tracer, run-shim, run-sw
patches/                       5 source patches
compat/sys/sysmacros.h         include shim
test/agx-offscreen-test.c      the test
test/agx-panel-demo.c          on-screen demo and benchmark
test/agx-wl-demo.c             GPU-rendered Wayland client
WAYLAND-NEEDS.md               what a Mesa Wayland client needs from the compositor
supervisor/                    guest scripts (darwin-agx-panel-demo.in is the demo's)
KERNEL-NEEDS.md                kernel-side gaps met by the Mesa work, with call sites
tools/cs-rehash.py             rewrite the page hashes of an ad-hoc code signature after relocation
trace/ioctl-trace.c            LD_PRELOAD ioctl/mmap logger
trace/summarize.py             condenses a log
trace/shim-g13g-trace.txt      recorded log
trace/shim-g13g-summary.txt    its condensed form
```
