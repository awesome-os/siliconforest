# IOGOPFramebuffer: describe the M1 firmware 30-bit framebuffer

Status: **source patch, not compiled, not booted.** It applies to PureDarwin
`e7aa00a` `src/Kernel/Extensions/IOGOPFramebuffer/IOGOPFramebuffer.cpp`. The
isolated bringup tree's copy is identical. Root owns the Nix build
(`build-framebuffer.nix`, `build-kc.nix` and `kc-display.nix` are root's
files).

## Bug

The M1 boot framebuffer arrives in boot-args as depth `0x1001e`: 30 bpp with 2x
scale, 2560x1600, stride `0x2800`. IOGOPFramebuffer's `getPixelFormats()` and
`getPixelInformation()` know only depths 8, 15, 16, 24 and 32, so depth 30
falls into the default case. That case reports `IO8BitIndexedPixels`, CLUT,
with `bitsPerPixel` 8.

IOFramebuffer computes the console depth as `componentCount * bitsPerComponent`
when `bitsPerComponent > 8`, and otherwise uses `bitsPerPixel`. At the console
handoff it therefore gives the kernel video console depth 8 for a buffer that
holds one 32-bit word per pixel.

## Fix (two added `case 30:` blocks, nothing removed)

- **`getPixelFormats()`:** returns `kIO30BitDirectPixels`.
- **`getPixelInformation()`:**
  - pixel format `kIO30BitDirectPixels`, type `kIORGBDirectPixels`;
  - masks R `0x3ff00000`, G `0x000ffc00`, B `0x000003ff`;
  - `bitsPerPixel` 32, `componentCount` 3, `bitsPerComponent` 10.

IOFramebuffer then derives console depth 30. XNU's video console paints depth
30 with `vc_paint_char_32` and its 10-bit colour column. There is no forced
32-bit reinterpretation: storage stays 32 bits and colour stays 10 bits per
component.

## Layout proof (pinned sources)

| Source | What it shows |
|---|---|
| m1n1 `src/fb.c` `rgb2pixel_30` | `(b << 2) \| (g << 12) \| (r << 22)`: R[29:20], G[19:10], B[9:0] in a native 32-bit word (little-endian in memory on arm64) |
| `IOGraphicsTypes.h` | `kIO30BitDirectPixels` = `"--RRRRRRRRRRGGGGGGGGGGBBBBBBBBBB"`, the same masks MSB-first |
| XNU `video_console.c` | `vc_color_index_table[30] = 3`; column 3 colours red `0x3FF00000`, green `0x000FFC00`, blue `0x000003FF` |

## Files

| File | sha256 |
|---|---|
| `framebuffer-30bit.patch` | `91086c74d740991f6f98a117f9fa957b211fe071255e77a0d2abab26fbd90654` |
| `test_framebuffer_30bit.py` | `858e16e50a1b6b6e36808fb9e7aff5a1be20930f164c6a4eaff3c70f65a181fb` |
| Resulting `IOGOPFramebuffer.cpp` | `3690a93a06a38cb708858df9756636c2c14bcb45e2d811b9274eda12fb974f99` |
| Pinned input `IOGOPFramebuffer.cpp` | `72bdea3925213de85ed7caeeba52ca38bac18dd6acc9807d4dde0571d0344773` |

```sh
python3 integrations/puredarwin/m1-display/test_framebuffer_30bit.py -v
```

11 tests pass. They check that:

- the patch applies to pinned e7aa00a, touches only IOGOP and removes nothing;
- the unpatched bug is real (depth 30 produces console depth 8);
- the patched description is exact and gives console depth 30;
- other depths are unchanged;
- the `kIO30BitDirectPixels` string decodes to these masks;
- the IOFramebuffer depth rule matches its source;
- the XNU colour table and the m1n1 `rgb2pixel_30` layout agree;
- the bringup tree's IOGOP file equals the pinned one.

Six mutants are caught:

- 8-bit components;
- 30-bit storage;
- swapped masks;
- a forced 32-bit format;
- a missing `getPixelFormats` case;
- a CLUT pixel type.

## Scope

This fixes only the handoff depth. It does not by itself route the console to
the panel. With `serial=3`, normal console output stays on the UART unless the
boot video display code is 2 and `serial_video_mirror=1` (host-side; see
`reports/sprint-010/framebuffer-path-review.json`). Buffered or early text can
still be painted by other paths.

Not covered: compile against the target SDK, KC inclusion, load and boot. The
first compile is root's.
