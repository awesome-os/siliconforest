# First GPU rendering under Darwin on the M1

Boot `ag05` (10 November 2026), `tools/agx_boot.sh ag05`, GPU-only collection
(`/nix/store/rsp58dxmnvj5s4prpvgadcy9smkhgi1q-puredarwin-kc-arm64-0.1`), boot arguments
`pdgpudt=2 pdgpudt_stringprops=1 pdagx=1`.

The off-screen GL test (`integrations/puredarwin/m1-mesa-asahi/test/agx-offscreen-test.c`),
through Mesa 26.3 asahi cross-built for Darwin and the Rust Asahi kernel driver in `PDAgx.kext`:

```
agx-offscreen-test: drm version name=asahi 0.0.0 date=0 desc="Apple AGX Graphics"
agx-offscreen-test: GL_RENDERER=Apple M1 (G13G B1)
agx-offscreen-test: GL_VERSION=OpenGL ES 3.2 Mesa 26.3.0-devel
agx-offscreen-test: frame A (clear) sha256=ec9ff145...28cae3f7 ... 0 of 65536 pixels differ: ok
agx-offscreen-test: frame B (triangle + textured quad) sha256=8fee6b41134a7b3eaa7e9b0f61aa6e907d21cafd4a2685a00a4699c3b32da274
agx-offscreen-test: frame B tolerance checks: ok
agx-offscreen-test: frame B exact hash vs --expect-b: ok
agx-offscreen-test: PASS
```

The frame B hash is the one the same test produced on this M1's GPU under Linux
(linux-aurora 7.2.11 + mesa-aurora 26.3), so the Darwin stack renders bit-identical output for
this scene: two render submissions and two compute submissions (readback) went through the
firmware; four firmware messages from endpoint 0x20 were logged.

What this is not: one small off-screen scene, one process, one run. No display path, no
compositor on the GPU, no Wi-Fi in this collection (the kext area is full), temporary
diagnostics still in the driver, and the DEBUG kernel printed ten non-fatal warnings
"IOMemoryDescriptor physical with managed page" with backtraces into PDAgx during the run.
Memory pressure handling (shrinker) and sysfs statistics are stubs. Not soak-tested.

Stack, bottom to top: `PDGpuDT` (power, device-tree properties) -> LinuxKPI layer with our
patches (`m1-rust-kext/lkpi/patches-layer`, 12 patches) -> DRM core at Linux 7.2 (`m1-drm72`) ->
RTKit/mailbox port (`m1-gpu-rtkit`) -> Rust-for-Linux kernel crate -> Asahi driver, 3 patch files
(`m1-rust-kext/agx`) -> PureDarwin's `PDDRMDri` render node -> libdrm -> Mesa asahi
(`m1-mesa-asahi`, 5 patches, 78 lines).

## Repeat test (boot `st01`)

`AGX_TEMPLATE=.../m1-mesa-asahi/supervisor/darwin-agx-stress-test.in tools/agx_boot.sh st01`:
40 sequential runs of the GPU test, each a new process with exact-hash check: 40 pass, 0 fail,
22 seconds in total. Then four processes at once: all four exit 0. No firmware timeout, crash
or panic in the kernel log. Process exit with live GPU state therefore works at this scale.

## Hardened driver, Wi-Fi and GPU together (boot `hd01`)

Collection with bridge + Wi-Fi + PDGpuDT + PDDRMDri + PDAgx (`tools/agx_boot.sh`, now the
default; 851,968 bytes free below the kernel after moving 850 KiB of zeroed layer tables out of
the image). Wi-Fi attached with its MSI as usual; the GPU repeat test passed 40 of 40 in 13
seconds and 4 of 4 in parallel; "firmware consumed its first channel command after 298 us"; the
"IOMemoryDescriptor physical with managed page" warnings are gone (0 in the log). One
diagnostic counter line still prints FAILED (`vmap through the owning descriptors: 0, refused: 0`):
the counter, not the mapping, is suspect; under investigation.
