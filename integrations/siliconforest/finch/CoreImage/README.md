# Finch CoreImage CPU source port (staged only)

All seven pinned implementation files: CIConstants,CIColor,CIImage,CIContext,
CIFilter,CIFilterTable,CIMisc. Own declarations come from source signatures,
public maintainer objc2-core-image0.3.2 type/hierarchy/property facts and our types;
no Apple SDK headers. build-facts.py/declarations.py reproduce them. The119 typed
built-in protocols retain scalar/point/rect/transform property encodings.

Build on Linux with Nix (usual nix-command/flakes feature flags):

```
nix-build --no-out-link integrations/siliconforest/finch/CoreImage/build.nix
nix-build --no-out-link integrations/siliconforest/finch/CoreImage/tests.nix
python3 integrations/siliconforest/finch/CoreImage/prepare.py \
  --framework LIBRARY_OUTPUT --tests TEST_OUTPUT \
  --stage NEW_STAGE --reference-packet NEW_PACKET
```

The helper creates an isolated framework plus portable Linux-built programs and
a binaries-only reference packet. Only the stage uses DYLD_FRAMEWORK_PATH;native
M5 reference runs strip DYLD overrides. Stage /private/var/db/sffinch/coreimage-cpu-v4
ran both programs on M1. Same-byte M5 references passed SHA256 checks,30s timeout,
core0,no installs and temporary-directory removal.

Focused ten-line probe MATCH: untagged RGBAf/RGBAh input/output,identity,invert,
exposure,typed color-control getters/setters,matrix,padded rows,signed/HDR values,
premultiplied alpha and lower-left crop. First M1 attempts faulted in retained
Onyx2D's float-CGImage sampler;failure receipts remain. Patch0002 keeps validated
floating bitmap data in a direct CPU recipe,checks dimensions/stride/length,
preserves half/float precision and converts named sRGB to the linear buffer.
Only unmanaged,sRGB/extended-sRGB and linear-sRGB/extended-linear-sRGB float
input is accepted;unsupported tagged float spaces return nil. It does not fix
other Onyx2D float-image callers or supply general ICC/gamut conversion.

Original391-line Finch comparison test runs with software rendering on both
systems (the same explicit option);line buffering is diagnostic only. Both exit0.
Raw comparison is DIFF: gray conversion returns nil when the real color-matching
provider is absent;infinite CGRect descriptions differ;PNG round-trip alpha/
channels differ. compare-pixels.py reports85/86 rendered pixel rows match using
upstream coarse quantization;the sole pixel difference is PNG round trip. It is
supplementary evidence and never changes the raw DIFF verdict or adds masks.
Metadata,typed-filter and remaining rendered outputs match in this test.

Color provider adapters preserve real Copy/Get ownership and recognize actual
O2ColorSpace objects;optional APIs/constants resolve dynamically. Missing non-RGB
conversion returns nil rather than fabricated black/identity conversion. Only
CoreImage and its program headers/sources are changed,not CoreGraphics/Onyx2D.

The canonical CoreImage path was absent on M1,however QuartzCore.pd defines nine
CI-family names (five static class collisions plus four generated filter names): CIAffineTransform,CIBloom,CIBoxBlur,CIColor,CIContext,CIFilter,
CIImage,CILinearGradient,CIVector. These tests do not load QuartzCore/AppKit.
The QuartzCore owner must reconcile that family before installation. No existing
provider was replaced and no new canonical framework was installed.

Unverified/excluded: GPU/Metal/OpenGL devices,kernel language compilation,real
CVPixelBuffer/IOSurface interop,detectors/ML,RAW providers,wide-gamut/profile
conversion,all filters and application integration. Some upstream surfaces are
placeholders;exported names do not establish their behavior. CPU evidence above
is limited to the actual programs and synthetic pixels.

The [15 November difference audit](../DIFFERENCES-2026-11-15.md) preserves those
original receipts and classifies their causes. A narrow real gray conversion
and a private Onyx PNG premultiplication correction make all 86 rendered rows
match. The remaining extent rows exposed an overflowing CoreGraphics constant
and incorrect geometry predicates/precision; `geometry-provider.nix` and
`geometry-build.nix` stage that dependency correction and rebuild CI with its
corrected inline declarations. These private dependency recipes do not edit or
install the canonical graphics providers. `diff-probes.nix` builds the independent
extent/geometry observation program for identical-binary M1/M5 checks.
Final private stage `coreimage-diffs-stage-v6` matches all three native programs,
including the original comparison and the new extent/rotation controls. The
earlier failure receipts above remain unchanged.
