## SF-AE: Metal experiment (Darling's Metal stack on the M1's Vulkan) — agent GFX2, 15 November 2026

Agreed with the owner as an experiment: Darling's Metal implementation on SiliconForest, on the M1's
GPU through Honeykrisp (SF-AC). Code: `integrations/puredarwin/m1-metal/` (README, NOTICE). Licences:
Indium and Iridium 0BSD, darling-metal MPL-2.0 per file (our changes to its files are published as the
`substituteInPlace` lines of `metal.nix` and stay MPL-2.0), mslc MIT; all fetched at pinned revisions,
nothing copied into git; nothing from the GPL-3.0 main Darling repository.

### Result

| Milestone | State |
|---|---|
| Indium + Iridium build for arm64 Darwin against our Vulkan | **built** (`indium.nix`; one 0BSD patch: the loader by its Mach-O name). Iridium dlopens the PureDarwin flake's LLVM 21 `libLLVM.dylib` (66.8 MB) by full path |
| Indium's `device-info` on the M1 | **observed**: 8 of 8 checks pass, device "Apple M1 (G13G B1)" |
| Indium's `basic-compute` with its own `add.metallib` (AIR translated by Iridium **on the M1**) | **observed**: "Compute results as expected" |
| Indium's `basic-compute-mslc` (mslc's SPIR-V for `add.metal`) | **observed**: a+b over 16,777,216 elements bit exact; both negative controls detect their faults |
| Indium's `cube` (window through GLFW, IndiumKit swapchain) | **built** (`indium-cube.nix`: the PureDarwin flake's GLFW recipe built for arm64, xkbcommon linked in); on the M1 GLFW's Wayland backend first stopped at `Failed to load libwayland-egl` (the M1 has none; now staged); with it (02:16Z): the "Indium Test" window (1000x750) appeared on the desktop and the test ended normally after 300 frames in 8 s (rc 0; only xkbcommon's missing Compose-file warning), the compositor's own check found the composited window identical to the client's buffer at commits 30 and 150, **but the captured window is black: no cube visible. Not correct**; why (Iridium's translation of `shaders.metallib` vs. the off-screen paths that work, depth/clear, the swapchain format) not established |
| darling-metal's Metal.framework (Objective-C ABI) for arm64 Darwin | **built** (`metal.nix`) against PureDarwin's libobjc, Foundation, CoreFoundation, with our header shims (`shim/`) |
| A Metal program of ours off-screen, by hash | **observed** (`sf-metal-tri`, our MSL compiled at run time by mslc through `newLibraryWithSource:`): a triangle into a 256x256 RGBA8 texture, blit to a buffer, **0 of 65,536 pixels** differ from the CPU rasterisation (fnv `0x086741dd` both, 19,082 pixels inside) |
| In a window through CAMetalLayer | **not done** (and Indium's own windowed cube draws black, above): our QuartzCore's CAMetalLayer needs PureDarwin's MetalKit backend (`PDMetalLayerBackend`), not built for arm64 here; IOSurface has no dma-buf export (below) |

All M1 runs: SiliconForest from its disk, kernel k2 (SF-AC), through `m1-vulkan/tools/sfvk-careful.sh`
(gui.lock, UART on the M5, kernel log streamed); Vulkan from the system install with no environment
variable (SF-AC, "Graphics in the default install"). Shaders: only the metallibs of Indium's own test
suite (Apple-toolchain output shipped in that project) and our MSL through mslc.

### What Iridium translates (workstation, `host.nix`, `mtl2spv`, LLVM 21)

All 12 metallibs of the Indium and darling-metal test suites translate. `spirv-val --target-env vulkan1.3`
passes 11; `function-constants.metallib` gives a module whose variable initialiser is not a constant (the
function constants are only specialised when a pipeline is made; `mtl2spv` makes none). Not tried: a
metallib of a real application (none may be copied out of Apple apps into tests here), AIR from newer
Xcode versions, argument buffers, ray tracing, mesh shaders, indirect command buffers.

### Problems found

- **A process that used Metal does not end.** The triangle passed in both runs whose output survived (0 of
  65,536 differ each time; in two more runs the output was lost: buffered, or in /tmp before a reboot),
  but in all four the process never ended, with a normal return or with `_exit`: it stays
  in state `?NE` (exiting) and `kill -9` does nothing; it goes away only with a reboot. Indium's device
  polling thread waits on a timeline semaphore with an infinite timeout (`pollEvents(UINT64_MAX)` ->
  `vkWaitSemaphores` -> a DRM syncobj wait); that wait is evidently not ended when XNU terminates the
  thread. A kernel item (LinuxKPI interruptible waits vs. thread termination; SF-AC open items), not
  darling-metal's. Other programs keep using the GPU meanwhile (`vulkaninfo` checked).
- PureDarwin's Foundation has no lightweight generics on `NSDictionary`'s subscript (returns `id`): two
  places in darling-metal picked another class's `-asIndiumDescriptor`; casts added (MPL-2.0 change).
- PureDarwin's Foundation declares `NSMethodSignature` in `NSInvocation.h`; PureDarwin's IOSurface has no
  `_IOSurfaceGetDMABuf`/`_IOSurfaceSetGPUCallbacks` (Darling's private interface): our shim answers "no
  dma-buf", so IOSurface-backed textures are copied, and an IOSurface cannot be imported zero-copy.
- mslc needs compiler-rt's 128-bit conversions (`__floatuntidf`), which our libSystem does not export.

### What a real MTLDevice for applications would still need

1. Metal.framework (and MetalKit) at `/System/Library/Frameworks` with install names there, Indium and
   Iridium in `/usr/lib`, libLLVM where Iridium looks; today everything is staged in
   `/private/var/db/sfmetal` and only our own program links it.
2. The exit hang fixed (polling thread woken and joined before Indium's finalisation).
3. Presentation: CAMetalLayer drawables into the compositor (PureDarwin's MetalKit `PDMetalLayerBackend`
   built for arm64, Wayland subsurfaces through WindowServer) and IOSurface <-> dma-buf (our IOSurface
   is shared memory; the compositor imports dma-bufs; Indium can import one, `newTextureWithDMABuf`).
4. Coverage for unmodified applications' metallibs (Iridium's AIR coverage, function constants,
   argument buffers), MSL source beyond mslc's subset, the device families and limits applications check.
5. Kernel/driver: our kernel reports `features = 0x1` (no exact-prior barriers, no ray-query render), so
   Honeykrisp takes the legacy G13 paths; GPU memory is not limited (`memory_limit = 0`); a client killed
   with work in flight still drops its mappings under running jobs on G13 (SF-AC).
6. Explicit sync to the compositor (no `wp_linux_drm_syncobj_v1`; presentation uses implicit sync).

### Revert

`rm -rf /private/var/db/sfmetal*` on the M1 (nothing of the system was replaced for Metal).

### Canonical framework policy (CODEX, 05:38Z, 15 November)

The owner subsequently authorized canonical Metal installation.
[Policy recipe and evidence](../../../integrations/puredarwin/finch/Metal/README.md)
retain all 185 darling-metal exports while returning nil/empty devices unless
`SF_METAL_ENABLE_REAL_DEVICE=1`. The disabled path returns before Vulkan/Indium
initialization. Real-device code remains intact and its exit hang is unresolved;
no new real-device run was made. Support libraries are inside the framework.

Cold target hashes were verified before activation, existing files kept
`.pre-finch`, and all installed-family regressions including compatibility18/18
passed. The unchanged Homebrew ffmpeg bottle now loads/decodes both codecs with
no DYLD overrides. The old experiment-directory revert above applies only to
that stage; canonical rollback uses its separate cold-install receipt at
`/private/var/db/sffinch/install-Metal-v1/receipt.json`.
