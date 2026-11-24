# GPU driver: first probe of the Rust Asahi driver under Darwin

Boot `ag02` (10 November 2026), GPU-only collection (`build-agx-kc.nix --arg wifi false`), boot
arguments `pdgpudt=2 pdgpudt_stringprops=1 pdagx=1`. `PDAgx.kext` is Omacom's Asahi driver
(Linux 7.2.11, common code + G13) compiled against the Rust-for-Linux kernel crate on the
compatibility layer; 2 small patch files against the driver.

Reached on hardware, with the driver's own log lines equal to Linux's on this machine:
bound through the platform bus -> GFX powered -> "Probing..." -> MMU/UAT initialised (kernel page
tables created, TTBs initialised, i.e. the handoff with firmware memory works) -> "GPU ID
registers: 0x4021100 0x40a04 0x30808 0x40404 0x1320300 0x101311" -> Type G13G, 8 cores ->
"Dynamic configuration fetched" (all device-tree properties accepted).

Stopped at: `HeapAllocator[Kernel Shared]::add_block: Failed to map at 0xffffffa800000000
(Error(12))`, the first GEM allocation mapped into the GPU's address space; probe returned -12.
No panic; `/dev/dri/renderD128` and `card0` appeared (from the DRM registration) but the test
found no asahi render node because the probe failed. Mesa's software path still passes.

Also found: the collection with Wi-Fi + PDAgx is rejected by the launcher ("Overlapping
file-backed segments map different bytes"): the kext area below the kernel is full.

## Boots `ag03`, `ag04`: probe completes, render node answers

`ag03` showed the layer's page allocator failing after about 1 MiB of single pages. The layer now
takes single pages from 1 MiB wired chunks (`lkpi/patches-layer/0011`); in `ag04` the pre-flight
allocated, marked, verified and freed 64 MiB (4096 pages) in 22 ms + 25 ms with no failures.

`ag04` result: the driver's probe completed. `[drm] Initialized asahi 0.0.0 for gpu@206400000 on
minor 0`, `PDAgx: attached`, RTKit initialised (protocol 12), four kernel GPU mappings made.
From user space on Darwin, through `/dev/dri/renderD128`:

- `drmGetVersion`: `asahi 0.0.0 "Apple AGX Graphics"`, as on Linux.
- `GET_PARAMS`: every field equal to Linux on this M1 (generation 13, variant G, revision 0x11,
  chip 0x8103, 8 cores, 1278000 kHz, VM range 0x4000..0x7fffff8000, features SOFT_FAULTS).
- Mesa's asahi driver then created its VM and buffers and stopped at the first `mmap` of a GPU
  buffer: `pd_cdev_mmap: driver refused 0x4000 bytes at 0x100238000 (19)`. EGL fell back to the
  software renderer, so the GPU pass of the test failed ("renderer does not contain Apple M1").

Not yet reached: user-space mapping of GPU buffers, job submission, any GPU rendering.
Run with `tools/agx_boot.sh <name>`.
