# M1 Wayland groundwork: SFGOP fail-closed contract and IOGOP write-combine opt-in

Status: **source patches with host tests; not SDK-compiled, staged or booted.**
Both patches apply to SiliconForest `e7aa00a`. They are separate from the frozen
first-display candidate (`m1-display/framebuffer-30bit.patch`, which stays
unchanged). The IOGOP patch also applies on top of that frozen patch. Root owns
staging and the native SDK/Nix builds.

## 1. `sfgop-fail-closed.patch` (SFGOP library)

The public `SFGOPFramebuffer` struct is **byte-identical**: 64 bytes, 8-byte
alignment, every offset unchanged. `_Static_assert`s in `SFGOP.c` pin the size,
the alignment and the key offsets. Existing C callers and Rust bindings built
against the original header keep working.

| Defect at the pin | Fix |
|---|---|
| `GetCurrentDisplayMode` returning success with `scalarOutCnt < 2` jumped to `fail` with `kr == 0`, so `SFGOPOpen` returned `KERN_SUCCESS` with a zeroed framebuffer. | Short reply fails with `KERN_FAILURE`, stage `GetCurrentDisplayMode:short`. |
| A short `IOPixelInformation` reply was accepted. | `pixelInfoSize` must equal the struct size (`GetPixelInformation:short`). |
| Geometry and mapping were not checked. | Requires nonzero width/height, bpp a multiple of 8 in 8..32, and `bytesPerRow >= width * bytes/px` (`PixelInformation:invalid`). Requires a nonzero mapping of at least `stride * height` (`MapVRAM:short`, after unmapping). |
| `SFGOPPresent` sent selector 11 to any service. On an IOFramebuffer user client selector 11 is `extSetGammaTable`. | New `SFGOPGetServiceKind()` classifies with `IOObjectConformsTo` in the form of whichever IOKit surface SFGOP is compiled against (see "Two IOKit surfaces" below). Present calls the driver only for virtio; a direct-memory framebuffer gets `KERN_NOT_SUPPORTED` and no driver call. No success stub. |

Other changes:

- **Query functions:** `SFGOPGetPixelFormat()` returns XRGB8888, XRGB2101010 or
  unknown from the existing `bpp` and `componentMasks`.
- **Fail closed:** a failed conformance query makes `SFGOPOpen` fail (`ServiceKind`).
- **Cache:** a one-entry kind cache keyed by the service port is dropped in
  `SFGOPClose`.
- **Unchanged:** the original file's `#ifdef _SF_IOKITLIB_H` / `#else` split
  is kept, and the new conformance call follows it.

### Two IOKit surfaces (correction)

SFGOP compiles against one of two different IOKit user surfaces. Each branch
matches its own *built* library; the hazard is pairing one surface's header
with the other surface's library.

| | (A) tiny static IOKit | (B) CF IOKit |
|---|---|---|
| Header | `src/Libraries/IOKit/include/SFIOKitLib.h`, via the tiny `IOKit/IOKitLib.h`; defines `_SF_IOKITLIB_H` | `iokituser/include/IOKit/IOKitLib.h` (Apple-shaped; installed by the `iokitcf-static` derivation) |
| Library | `libIOKit.a` from `src/Libraries/IOKit/IOKitLib.c` | `libIOKitCF.a`, and `libIOKitCF.dylib` via `-force_load`, from `iokituser/IOKitLib.c` |
| Matching | `char *IOServiceMatching`; `IOServiceGetMatchingService(master, char *, io_service_t *)` returns a status | CFDictionary `IOServiceMatching`; `IOServiceGetMatchingService(master, CFDictionaryRef)` returns `io_service_t` |
| `IOObjectConformsTo` | `kern_return_t (obj, name, boolean_t *conforms)` (3 arguments) | `boolean_t (obj, name)` (**2 arguments**) |
| SFGOP branch | `_SF_IOKITLIB_H` | `#else` |
| Current users | fbdoom, xf86-video-siliconforestgop (local IOKit), root's planned static presenter | the wlroots build (`-I${iokitHeaders}` = iokitcf-static headers, `-lIOKitCF`) |

**Evidence for the CF arity:** the built `libIOKitCF.a` (store paths `l580vwcv…`
and `nv25rfc8…`) contains only `IOKitLib.c.o`. Its `_IOObjectConformsTo`
disassembly reads `w0` and `x1`, sets `x2 = 0` itself and calls
`__IOObjectConformsTo(obj, name, uint64_t options)`. That third argument is an
options value, not a `conforms` pointer.

**Not built:** `src/Libraries/IOKit/IOKitLibCF.c` defines a 3-argument
pointer-form `IOObjectConformsTo` (line 674), but no CMake target compiles it at
this pin. Earlier statements that the CF library's conformance call is
3-argument were wrong for the built library.

**Mis-pairing consequences:**

- **CF header + tiny library:** the callee writes through whatever is in `x2`,
  which is memory corruption.
- **Tiny header + CF library:** the callee ignores `x2` and returns a boolean
  that the caller reads as a status. SFGOP then reports an unknown kind and
  `SFGOPOpen` fails closed.

Matching calls mismatch in either mis-pairing. Build each consumer
against exactly one surface's header and library.

**Optional hardening, not applied:** classify with `IOObjectGetClass(service,
io_name_t)`, which has the same `kern_return_t` signature on both surfaces
(`SFIOKitLib.h:90`; `iokituser/IOKitLib.h:256`). Compare against
`"IOVirtIOGPU"`; nothing derives from it at the pin. That removes the arity
divergence for the kind query; matching still differs by surface.

**Callers (rebuild needed, no source change required):**

- the wlroots siliconforest backend, which copies `SFGOP.c`/`SFGOP.h` in `wlroots.nix`;
- `xf86-video-siliconforestgop`, whose `(void)SFGOPPresent` now returns
  `KERN_NOT_SUPPORTED` on IOGOP instead of hitting the gamma method;
- fbdoom.

## 2. `iogop-vram-write-combine.patch` (IOGOPFramebuffer kext)

**Gate.** The opt-in applies only to the Apple SoC firmware framebuffer: a
platform nub (not `IOPCIDevice`) under a device tree whose root `compatible`
contains `AppleARM`. The SiliconForest Apple platform expert matches the same
string. Inside the gate, before `fb0_publish` and `registerService`, the kext:

- sets `sf-vram-write-combine = true`. `IOFramebufferUserClient` already
  honours this property on arm64 for the `kIOFBVRAMMemory` user mapping (both
  `clientMemoryForType` paths); nothing set it before.
- maps `/dev/fb0` with `kIOMapWriteCombineCache` instead of
  `kIOMapInhibitCache`.

Non-Apple platforms keep their existing modes.

### Cache-attribute rationale (source anchors are test-checked)

**Facts at the pin:**

- **Where the framebuffer lives:** 0xbd5d5c000 + 0xfa0000, inside the ADT DRAM
  range (`/chosen dram-base 0x800000000`, `dram-size 0x400000000`), so
  `is_dram_addr` is true. It is above guest XNU-managed RAM, so it is not
  `pa_valid` and has no physmap (cacheable) alias. No ADT `pmap-io-ranges` entry
  covers it (109 entries checked).
- **Default and InhibitCache:** IOKit's default for a physical range is
  `IODefaultCacheBits` → `pmap_cache_attributes` → `VM_WIMG_IO`.
  `kIOInhibitCache` is also `VM_WIMG_IO`. For DRAM, `wimg_to_pte` turns that into
  `CACHE_ATTRINDX_POSTED_COMBINED_REORDERED` (MAIR 0x0D, **Device-GRE**).
- **WriteCombine:** `kIOWriteCombineCache` is `VM_WIMG_WCOMB`, which for DRAM
  becomes `CACHE_ATTRINDX_WRITECOMB` (MAIR 0x40, **Normal non-cacheable**).

**Aliases of this buffer:**

| Alias | Before | After this patch |
|---|---|---|
| XNU boot console (`initialize_screen`, `HAS_UCNORMAL_MEM`) | Normal-NC (`VM_WIMG_WCOMB`) | unchanged |
| compositor `kIOFBVRAMMemory` user map | Device-GRE | **Normal-NC** |
| `/dev/fb0` kernel map (`fb0_write` → `uiomove`) | Device-GRE | **Normal-NC** |
| IOFramebuffer `fVramMap` (`kIOFBMapCacheMode = kIOMapInhibitCache` on arm64; post-handoff console, cursor) | Device-GRE | unchanged (no global IOGraphics change) |

**Correctness, not just speed:**

- **Device memory faults on unaligned access and on `DC ZVA`:** a compositor
  that renders straight into VRAM with ordinary memcpy/memset/pixman (wlroots
  copy path, a Rust backend writing 2101010) can take alignment faults (SIGBUS)
  through a Device-GRE user map. Normal-NC allows those accesses.
- **`/dev/fb0` (mode 0666):** writes at unaligned offsets are unaligned kernel
  stores into Device memory. They land inside copyio, whose recovery handler
  turns the alignment fault into `EFAULT` (`handle_kernel_abort`); outside a
  recovery range the same fault panics ("Unaligned kernel data abort").
  Normal-NC removes the fault.
- **Mismatched-alias correctness:** Device-GRE and Normal-NC aliases of the same
  DRAM are both non-cacheable, so there is no cache-coherency loss and no
  cacheable alias exists. What mixing loses is cross-alias ordering and gathering
  guarantees. Normal-NC also permits speculative reads, which are harmless for a
  DRAM buffer with no read side effects. With the one-owner-at-a-time hand-off
  (kernel console releases on server connect and re-acquires on close), mixing
  is visual-ordering only, not data corruption.
- **Why `fVramMap` stays Device-GRE:** the kernel console reaches it only with
  aligned 32-bit stores (`vc_paint_char_32`, the `vc_clear_screen` store loop) and
  `bcopy` of whole, 16-byte-multiple rows (`video_scroll_up`). That works but is
  slow, because scroll reads Device memory. Making it Normal-NC would mean
  honouring the same property in IOGraphicsFamily's `fVramMap` mapping: a
  property-gated but shared-code change, left for a separate review.

The display controller does not snoop CPU caches, so a cacheable mapping would
scan out stale lines. Write-combining (Normal-NC) matches the boot console and
avoids that.

## Tests

```sh
python3 integrations/siliconforest/m1-wayland/test_m1_wayland.py -v
```

15 tests pass on the host.

**SFGOP:** the real patched `SFGOP.c` is compiled with `cc` against
`hosttest/fake` headers that mirror `SFIOKitLib.h`, and driven through 14
scenarios. The checks:

- ABI 64/8 with the same offsets, both pinned and patched;
- the struct definition is textually identical;
- the SiliconForest 3-argument conformance call is used;
- the patched build is warning-free;
- each failure stage fails, zeroes the struct and cleans up;
- present classification and the cache drop on close;
- the pinned original's defects reproduce (short replies return success;
  selector 11 reaches an IOFramebuffer);
- 7 recompiled mutants are caught.

**IOGOP:**

- the gate (PCI excluded, AppleARM required) and its placement before
  `fb0_publish`/`registerService`;
- the non-Apple default is unchanged;
- the patch stacks on the frozen 30-bit patch;
- the property name equals the user-client reader's;
- rationale anchors in IOMemoryDescriptor, iokit_rpc, pmap, proc_reg,
  video_console, IOFramebuffer and vm_param;
- 5 mutants are caught.

## Not covered

- Native SDK compile, kext/KC inclusion, runtime attributes on hardware, and
  compositor behaviour.
- W1 (wlroots 10-bit primary format): deferred. It is needed only for a Sway
  baseline; the Chonkstep backend renders 2101010 directly.
- No GPU, display-controller programming, vblank or reset path is involved or
  claimed.
