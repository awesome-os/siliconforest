Eight whole numeric source groups build: vDSP, vMisc, BLAS, LAPACK, Quadrature,
LinearAlgebra, SparseBLAS and Sparse. They remain staged; no Accelerate framework
was installed. Build them with `nix-build --no-out-link build.nix --arg groups
'[ "vDSP" "vMisc" "BLAS" "LAPACK" "Quadrature" "LinearAlgebra" "SparseBLAS" "Sparse" ]'`.
The default whole-framework recipe additionally compiles all 18 vImage C files.
`installed-graphics.nix` now builds that complete tree against Cocoa's real
ICC/indexed-color provider, preserving the bootstrap recipe for old receipts.
The whole umbrella has explicit mappings for all eight vecLib component IDs.
`vimage-tests.nix` builds Finch's original image comparison program using our
headers; it completes on the M5 with the same Linux-built bytes prepared for
the M1. The corrected private stage emits 1,159 output lines: 1,158 match native;
Display P3 coarse conversion is 80 versus 82 (DIFF). The initial missing nested
framework-path loading failure remains recorded. Whole Accelerate is now installed
and passes Root/VT/HAL/desktop/app-open/compatibility regressions. The final recipe
rebuild reproduces all 40 installed files byte-for-byte. See
[installed-media evidence](../media/INSTALLED.md); general numerical equivalence
is not established.

Our C declarations come from preprocessed Finch definitions through
`tools/c_functions.py`; typed headers and Sparse overload glue are ours.
BLAS's original ABI generator consumes our signatures/symbol list. No SDK
headers, inline implementations or .tbd files are used. Public numeric/layout
facts were checked against maintainer bindings and Linux-built runtime probes
on native macOS. Our catalogue is not the complete Apple SDK API catalogue.

Identical Linux-built comparison binaries run on M1 and the authorized M5.
Printed results match exactly for vDSP (300 lines), vMisc (207), BLAS (614),
SparseBLAS (348) and Quadrature (40). Independent M1 vector checks cover FFT,
biquad, vForce, 32/64-bit and legacy/new BLAS ABIs and LAPACK LU/inverse.
Sparse direct/iterative solver assertions pass on both systems. The 62-line
Sparse comparison differs on FGMRES with a diagonal preconditioner: native
Accelerate reports a parameter error, while Finch converges. Keep this DIFF.
The [15 November audit](../DIFFERENCES-2026-11-15.md) isolates this to the native
enum overload: the same factory-created diagonal preconditioner succeeds through
its opaque overload. The port's genuinely missing matrix+opaque overload is now
implemented over the real solver and checked with the same Linux-built program
on M1 and M5. No rejection is added just to imitate the native error.
Printed comparisons do not establish general numerical accuracy equivalence.

LinearAlgebra's numeric/lifetime results match; its 247-line comparison differs
only in superclass: our real NSObject-backed OS_la_object adapter replaces the
unavailable private OS_object allocator/metaclass. Full private OS_object
interoperability is not established. Quadrature workspace bounds and enum values
were checked with native runtime probes; our QAGS workspace macro is a conservative
budget, not a copied SDK macro. Compiler-rt supplies complex arithmetic helpers.
Our local sinpi/cospi/tanpi reduction preserves integer/half-integer values;
compile it without builtin folding. vDSP rounded int16 conversion saturates.

`tests.nix`, `comparison.nix`, `object-tests.nix`, `sparse-tests.nix` and
`solver-tests.nix` build the corresponding CLI checks here on Linux.
`reference.nix` builds the runtime ABI/workspace probes. Reference execution
uses fresh /tmp directories, SHA256 verification and 30-second timeouts, with
no Mac builds, installs, DYLD overrides or Apple files copied back.

Our CoreVideo numeric fallback now guards each macro name independently, so
it coexists with the media declarations. The five additional public dither/
multidimensional-table values are authored numeric declarations checked against
Finch's mode implementation and the maintainer's
[Go bindings](https://pkg.go.dev/github.com/deploymenttheory/go-bindings-macosplatform/bindings/internal/raw/frameworks/accelerate).
No SDK headers were imported.

The separate [Display-P3 audit](P3-DIFFERENCE-2026-11-15.md) resolves the retained
80/82 difference as a real adapter bug: named CG P3 had no ICC bytes and vImage
fell back to the sRGB profile. The narrow named-P3 fallback now uses our existing
real ColorSync P3 profile. Private baseline/fixed M1 and same-byte M5 controls
verify named/explicit/generated profiles; the original full vImage test now
MATCHes native with coarse82. A new detailed probe retains its raw DIFF for
ICC availability and small CMM arithmetic differences (one U8 component by one
LSB; max F32 error0.000045597), equally present in explicit-profile controls.
No new tolerance or mask is added; this audit changes no canonical provider.
