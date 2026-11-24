# QuickTime interface evidence, 2026-11-15

Target is the already-held QuickTime Player26.5.2 universal executable under
ignored `private/mac-apps/m5-26.5.2/`. No new Apple executable or SDK file was
obtained. REA opened that exact artifact (SHA256
`9fff9ad29b20ab75f88a4e3f4d7515e50852ffc8e199e08c2ff3d3c6e8d5c203`).
REA's deep providers are unavailable here: Ghidra unconfigured, Hopper absent,
IDA unconfigured. The observations below are ordinary local LLVM inspection,
not REA decompilation or runtime evidence. No provider was installed or client
configuration changed.

The arm64e import-stub table and direct call sites establish these calling
conventions. They do not establish implementation semantics:

| Operation | Observed argument/return layout | Local call sites |
|---|---|---|
| AVAssetSlowMotionTimeRangeFromNormalizedSlowMotionTimeRange | 48-byte input range, 24-byte duration, 48-byte indirect return | `0x1000130f0`, `0x10008f610` |
| AVNormalizedSlowMotionTimeRangeFromAssetSlowMotionTimeRange | Same layout | `0x100012ff0` |
| FigGetRotationAngleAndFlipsFromCGAffineTransform | 48-byte six-double transform passed indirectly, two independently addressed one-byte outputs; signed32-bit return | `0x10000a1d4` |
| MTCopyLocalizedStringForVideoDynamicRange | Scalar from CMVideoFormatDescriptionGetVideoDynamicRange; returned object/string consumed and released | `0x100065eac`, `0x100065efc` |
| MTCopyStringForColorInfo | FourCC `vide` and the CoreMedia description returned by `formatDescription`; returned optional string | `0x100065078` |

The range layout matches our independently declared public CMTimeRange and
CMTime structures. `math-reference.c` uses our own declarations and source-owned
rational ranges/transforms to test the first four operations. Its transform
output buffers have guards so a wrong one-byte-width inference is visible.
Dynamic-range labels and numeric rotation results were collected with the
same Linux-built probe on M5 and M1; expanded vectors match exactly. The selector stub at `0x1000d72e0` is
`formatDescription`; its result also goes directly to the public
CMVideoFormatDescriptionGetVideoDynamicRange call. The color probe therefore
uses descriptions we construct with public CoreMedia calls. FigAssetCreateWithURL
is only taken by address in this app's initialization; no direct call supplies
its argument contract, so it remains uncalled and unimplemented.

Raw offline evidence stays in ignored `runs/sffinch/`:
`quicktime-arm64e-indirect-v1.txt`, `quicktime-private-call-sites-v1.json` and
`quicktime-rotation-call-v1.asm`. The latter is a local disassembly, not a
file copied from the controller. The source/recipe was built after CODEX-FX's stability hold. Completed M5 runs
retained the usual
fresh temporary directory, Linux build identity/SHA check,30s bound, no DYLD
overrides and cleanup; only our program's observations come back.
