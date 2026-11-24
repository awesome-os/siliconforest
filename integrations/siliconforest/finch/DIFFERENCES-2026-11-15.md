# Media comparison differences, 15 November 2026

This classifies retained raw comparisons. It does not mask their output or
replace their DIFF verdicts. Programs are built on Linux from our declarations;
native runs verify SHA-256 in a fresh temporary directory, run with a 30-second
bound and no DYLD overrides, then remove the directory. Only program output is
returned. Target changes below are private stages; no installed provider is
changed by this audit.

| Group | Classification | Evidence and action |
|---|---|---|
| AAC/MP3 decode, AudioToolbox and AVFAudio | Acceptable decoder/PNS and final rounding variation for these fixtures; not an API framing/seek bug | Both decode 12000 valid stereo 48 kHz frames, with matching seek and zero-lag alignment. Full PCM measurements and a no-PNS control below. Raw lossy comparisons stay DIFF. No decoder samples are changed to imitate one implementation. |
| CoreImage raw comparison | Three real bugs fixed and verified in a private prefix | Generic-gray conversion returned nil because the provider lacked a matcher. A narrow actual tone-curve conversion fixes it. PNG alpha was clamped instead of premultiplied in Onyx2D; a private dependency patch fixes it. All 86 rendered rows, original raw comparison and independent extent/geometry probe now MATCH native after fixing gray conversion, PNG premultiplication, and CoreGraphics geometry. Old DIFF receipts remain unchanged. |
| Accelerate LinearAlgebra | Acceptable private class-hierarchy implementation difference | All numeric/lifetime rows match; the superclass is NSObject rather than OS_object. Public la_retain/la_release contracts explicitly use Objective-C retain/release messages, which the real adapter implements. No fake OS_object class is introduced just to change a printed name. Private dispatch allocator interoperability remains unclaimed. |
| Accelerate Sparse FGMRES | Acceptable additional convergence: native enum-overload limitation, with a separate real API gap fixed | Native rejects the enum overload but accepts the same factory-created preconditioner through the documented opaque overload. Finch solves both correctly. Our genuinely missing matrix+opaque overload is now implemented and verified. No blanket rejection is added merely to reproduce an error string. |

## Lossy audio measurements

`AudioToolbox/codec/pcm-compare-probe.c` emits each decoded sample as program
stdout. `compare-pcm.py` verifies identical binaries, rate/channel/frame counts,
ordered sample indices and S16 range before calculating error. These are our
short generated tones and a native decoder comparison, not the normative ISO
reference waveforms; no general ISO or 3GPP conformance is inferred.

| Fixture | Left max/RMS error, S16 LSB | Right max/RMS error, S16 LSB |
|---|---:|---:|
| WAVE |0 /0 |0 /0 |
| ALAC |0 /0 |0 /0 |
| MP3 |1 /0.121106 |1 /0.102875 |
| AAC, default encoder PNS |5 /0.624233 |12 /0.956948 |
| AAC, encoder PNS explicitly disabled |1 /0.009129 |1 /0.012910 |

The no-PNS fixture differs in just one left and two right samples. Disabling
PNS is a controlled encoder change, not a comparison mask or decoder patch.
For the original AAC the zero-lag RMS is below one LSB, whereas a one-sample
shift produces 53/81 LSB RMS; this excludes a hidden one-sample priming shift in
this observation. AVAudioFile delegates to the same ExtAudioFile decoder path,
and its retained compressed-converter outputs agree on frame and packet counts.

The standards allow computational error bounds rather than requiring every
floating/fixed-point implementation to emit identical bits. AAC PNS has separate
spectral/temporal energy criteria; applying a literal PCM hash test to its noise
is not the PNS conformance test. The measured PNS-disabled and MP3 errors are
consistent with final synthesis/rounding variation. This classification is
limited to these retained differences; it is not a claim that every bitstream,
profile, channel layout or malformed input conforms.

Primary technical references:
[ISO audio-conformance scope](https://www.iso.org/standard/86376.html),
[3GPP/ETSI decoder objective-criteria policy, section 5](https://www.etsi.org/deliver/etsi_ts/126400_126499/126406/19.00.00_60/ts_126406v190000p.pdf),
[ISO-authored PNS criteria, section 6.6.1.2.2.4](https://iso.ieclist.ink/iec/ISO%20IEC%2014496-4-2004%20PDF.pdf).
The last link is an ISO-authored document hosted by a third party; its criteria
are discussed, not redistributed as test inputs.

Raw evidence: `runs/sffinch/media-diffs-stage-v1/{candidate,audio-quantified}.json`,
`m5-audio-pcm-diff-v1-reference.json` and `m5-audio-no-pns-v1-reference.json`.
Reproduce the no-PNS control using the original tone.wav and the same command as
`codec/fixtures.sh`, adding `-aac_pns 0` to its AAC encode command.

## CoreImage bugs and representation

The unsupported gray result was a functional gap. `SFCIColorProvider.h` now
performs a narrow neutral Generic Gray/linear-gray to sRGB/linear-sRGB transform
when a real general provider is absent. The retained unprofiled monochrome
space adopts Generic Gray as its device default. Unknown ICC-tagged inputs,
CMYK and arbitrary gamut matching remain unsupported rather than becoming an
identity or black value. The original native row 0.572307 now matches exactly
on the M1, with the independent floating/HDR/alpha/stride probe unchanged.
The named extended-gray branch uses gamma 2.2, as specified by the public
[extendedGray definition](https://developer.apple.com/documentation/coregraphics/cgcolorspace/extendedgray).
Out-of-range extended gray and ICC-tagged gray are explicitly unsupported by
this narrow fallback; they are not silently clamped or treated as identity.

The PNG source advertises premultiplied pixels after loading straight RGB.
Replacing `MIN(channel, alpha)` with a rounded channel-times-alpha conversion
preserves the actual color. The [PNG specification](https://www.w3.org/TR/png/#6AlphaRepresentation)
defines unassociated samples; clamping cannot implement that contract.
`CoreImage/png-provider.nix` stages the corrected Onyx2D as a private framework.
The first dependency stage used a dylib search directory, which did not override
the framework import; that failed 85/86-pixel result is retained. Corrected
framework packaging in `coreimage-diffs-stage-v2` now matches all 86 rendered
rows with the same binary as the native reference (SHA-256
`c907670d5f1ab4cca3c7ebaea250de9c3ec9b1313a05d3ac562e52df78cb219a`).
TERM owns canonical Onyx2D integration and guarded installation. Neither
CoreImage nor Onyx2D was installed by this audit.

The six unbounded-extent rows are a **real CoreGraphics dependency bug**, not
merely acceptable formatting. The retained initializer spells the width as
`1.79769313486232e+308`, larger than `DBL_MAX`, so it rounds to IEEE infinity.
The same rounded decimal in the inline predicate also makes it fail on native's
finite canonical sentinel. `CGRectIsInfinite(CGRectNull)` additionally returns
true in that inline despite native returning false, and the union implementation
uses 32-bit float temporaries for 64-bit CGFloat extents.

The private geometry patch uses exact `DBL_MAX`/`CGFLOAT_MAX` expressions,
corrects one-axis empty rectangles, removes the null-as-infinite inline case,
and uses CGFloat temporaries for union/integral. The independently implemented
runtime export in `CGGeometryExports.m` needs its own precision correction;
changing the header alone cannot fix that body. Native `extent-probe.m` observations verify
finite canonical bits, runtime infinite/null/empty predicates, large-point
containment and finite intersection/union; M1 baseline and final fixed-prefix checks are complete. The [CGRectInfinite contract](https://developer.apple.com/documentation/coregraphics/cgrectinfinite)
and [CIImage extent contract](https://developer.apple.com/documentation/coreimage/ciimage/extent)
are geometry APIs, so finite-crop pixels alone cannot qualify these semantics.
Neither CoreGraphics nor its owner recipe is changed canonically by this audit.
The first geometry stage (`coreimage-diffs-stage-v5`) makes the original full
comparison MATCH, but the new semantic probe still catches the independent
runtime predicate and an overflowing rotated constant image. That failure
revealed a relative header-directory symlink preserved during private packaging:
CI had silently fallen back to its old declarations. The corrected recipe
dereferences the headers and recompiles CI. Final `coreimage-diffs-stage-v6`
now reports MATCH for the original full comparison, the focused CPU probe and
the semantic extent probe. All 86 rendered rows and all six extent rows match;
canonical/null/empty predicates, large-point containment, intersection/union and
the rotated constant image match too. The extent probe SHA-256 is
`43d36e82f3829c97f121b7aecfc674b4c1f79151506fa1bcdc2383e00f3a435f`
on both machines. The stage verified the SHA-256 of all three private providers
on the M1 before any test executed them.

The probe deliberately retains both old compiled inline observations and public
runtime calls: old inlines misclassify native too, so matching those lines alone
would not prove correctness. Runtime calls now match native, and CI itself was
rebuilt against the corrected inline declarations before its transformed-image
case passed. This is a bounded geometry audit, not qualification of every
negative-sized, disjoint or nonfinite rectangle operation.

## LinearAlgebra ownership contract

The [la_retain](https://developer.apple.com/documentation/accelerate/la_retain(_:))
and [la_release](https://developer.apple.com/documentation/accelerate/la_release(_:))
contracts describe ordinary Objective-C messages on the modern runtime. The
adapter uses real allocation, retain/release and deallocation of its numeric
storage. Existing views keep shared storage alive; the original 247-line suite
matches every result except the private superclass name. Private class ancestry
and using the object with unrelated libdispatch internals are outside that
observed public lifetime contract.

## Sparse preconditioner dispatch

`Accelerate/preconditioner-probe.c` uses a general CSC matrix and the same
symmetric lower-triangle matrix, both representing `[[4,1],[1,3]]`, with
`b=[1,2]`. Its solution is `[1/11,7/11]`; all reported converged solutions
have residuals below `1e-7` at the printed precision. DQGMRES, GMRES and
FGMRES are exercised independently with None, a real user callback, factory
diagonal and factory diagonal-scaling preconditioners. The user callback's
memory remains caller-owned; factory objects are cleaned up normally.

The factory returns a nonnull implementation and memory for both built-in
types on the M5. Native FGMRES rejects those types only when supplied as the
enum argument; passing the factory object through the opaque overload
converges. Its user callback also converges. The same Linux-built binary on
SiliconForest converges through both preconditioned overloads. No-preconditioner
FGMRES correctly fails on both systems; the other methods match. These controls
exclude the original suspicion that the native factory silently returned None.
They demonstrate an overload-specific native restriction, rather than an
invalid diagonal preconditioner or an error in our converged solution.

Apple documents both [matrix RHS](https://developer.apple.com/documentation/accelerate/sparse-iterative-solving-functions-matrix-rhs)
and [vector RHS](https://developer.apple.com/documentation/accelerate/sparse-iterative-solving-functions-vector-rhs)
overloads accepting an opaque preconditioner. That overload was absent from
our glue. `sparse-overloads.py` now adapts the sparse matrix to the existing
real multiplication/iterative-solver path for all four scalar families and
both RHS forms. Double-vector runtime behavior is verified here; the selected
Sparse/SparseBLAS build compiles all generated variants. There is no public
requirement in these documented contracts to reject an enum preconditioner
that produces a correct solution, so the extra convergence is retained.
Applications depending on the native error are an observable compatibility
limitation, and the raw DIFF remains.

Raw controls: `sparse-diffs-stage-v2/candidate.json` and
`m5-sparse-preconditioner-v4-reference.json`; identical program SHA-256
`3ebb66ff0b5f09569c28710c21e19a4b8bfa4425be0e3e00f951d996033190df`.
The earlier unsupported `SparseCreatePreconditioner(None)` control trapped
on native; its failed receipt is preserved and it is not an API success claim.

## Reproduction and adoption

Run from the repository root, with the existing Nix setup:

```sh
env SF_PD=tip /home/chrisk/.nix-profile/bin/nix-build --extra-experimental-features 'nix-command flakes' integrations/siliconforest/finch/AudioToolbox/codec/diff-probes.nix --no-out-link
env SF_PD=tip /home/chrisk/.nix-profile/bin/nix-build --extra-experimental-features 'nix-command flakes' integrations/siliconforest/finch/Accelerate/diff-probes.nix --no-out-link
env SF_PD=tip /home/chrisk/.nix-profile/bin/nix-build --extra-experimental-features 'nix-command flakes' integrations/siliconforest/finch/CoreImage/build.nix --no-out-link
env SF_PD=tip /home/chrisk/.nix-profile/bin/nix-build --extra-experimental-features 'nix-command flakes' integrations/siliconforest/finch/CoreImage/png-provider.nix --no-out-link
env SF_PD=tip /home/chrisk/.nix-profile/bin/nix-build --extra-experimental-features 'nix-command flakes' integrations/siliconforest/finch/CoreImage/geometry-build.nix --no-out-link
env SF_PD=tip /home/chrisk/.nix-profile/bin/nix-build --extra-experimental-features 'nix-command flakes' integrations/siliconforest/finch/CoreImage/diff-probes.nix --no-out-link
python3 integrations/siliconforest/finch/AudioToolbox/codec/compare-pcm.py runs/sffinch/media-diffs-stage-v1/candidate.json runs/sffinch/m5-audio-pcm-diff-v1-reference.json runs/sffinch/m5-audio-no-pns-v1-reference.json --output runs/sffinch/media-diffs-stage-v1/audio-quantified.json
python3 integrations/siliconforest/finch/CoreImage/compare-pixels.py runs/sffinch/coreimage-diffs-stage-v6/candidate.json runs/sffinch/m5-coreimage-v4-reference.json --output runs/sffinch/coreimage-diffs-stage-v6/pixel-comparison.json
```

Comparison binaries are normalized to public framework imports with
`finch/tools/portable-tests.py`, re-signed, then those identical bytes are used
on both machines. The retained stage manifests record the precise private
prefixes and arguments. The final combined native receipts and raw verdicts are
`coreimage-diffs-stage-v6/reference.json` and `comparison.json`; its
`provider-hashes.json` and `capture.stdout` record the cold provider checks. M5 capture uses `runs/sffinch/run-m5-references-v2.py`;
M1 capture uses `run-next-stage.py`, under `gui.lock`, bench serialization and
its coordination gate. A deadline is not permission to cross another owner's
announced kernel/install/test hold.

CoreImage's gray fallback and Sparse's overload are ready for owner review.
The PNG and geometry patches must be adopted into the canonical Onyx2D and
CoreGraphics recipes by their owner, then requalified with the whole graphics
stack. Rebuild CoreImage against the corrected geometry header too: fixing only
the shared-library constant cannot repair predicates already inlined into a
client. `geometry-build.nix` demonstrates this dependency for the private test.
The separate retained vImage Display-P3 result (80 versus82) received a later
[profile/provider audit](Accelerate/P3-DIFFERENCE-2026-11-15.md). It was a real
named-profile selection bug, now fixed privately: named P3 without ICC data
fell back to sRGB. The unchanged full vImage comparison now matches82; focused
component metadata/arithmetic differences remain raw and documented. This
follow-up does not alter the original four groups or their raw receipts.
