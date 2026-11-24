# Staged MediaToolbox utilities

**QuickTime work parked 2026-11-15 by owner direction:** Firefox media takes
priority. Keep existing source, stages and installed providers unchanged.
The unestablished FigAssetCreateWithURL ABI remains unresolved; no guessed
success implementation or new QuickTime load attempt is scheduled.

Linux/Nix builds use our own declarations and the installed media families.
Five of QuickTime's seven reviewed imports are provided, with real operations:

* MTCopyLocalizedNameForMediaType and MTCopyLocalizedNameForMediaSubType create
  owned CFStrings using English format names. Unknown nonzero FourCCs retain
  their four characters; zero returns NULL. The public-API matrix includes
  mismatched types, audio/video formats, text and unknown codes.
* FigGetRotationAngleAndFlipsFromCGAffineTransform recognizes orthogonal
  rotations/reflections and writes independent one-byte flip outputs. It
  returns degrees; degenerate/nonorthogonal cases follow observed native
  zero/false behavior. Vectors include scale and floating-point perturbations.
* MTCopyLocalizedStringForVideoDynamicRange copies the observed names for
  values0..8 and returns NULL for unrecognized values.
* MTCopyStringForColorInfo reads actual CoreMedia description extensions and
  emits color-primary/transfer/matrix code points. Missing descriptions return
  NULL; unspecified codes are2. It does not infer color from image pixels.

The two public format-name declarations follow Apple's documentation for
[media types](https://developer.apple.com/documentation/mediatoolbox/mtcopylocalizednameformediatype(_:))
and [subtypes](https://developer.apple.com/documentation/mediatoolbox/mtcopylocalizednameformediasubtype(_:_:)).
Private calling layouts are recorded in `../AVKit/PRIVATE-ABI.md`. Our own
Linux-built programs ran against native M5 and the M1 prefix. Expanded math,
color and format-name vectors matched exactly in
`runs/sffinch/media-math-stage-v1/comparison.json`. These are tested vectors,
not complete private-API conformance or locale coverage. No Apple SDK headers
or native implementation bytes are build inputs.

FigAssetCreateWithURL remains absent: the reviewed application only takes its
address, which does not establish its calling contract. The professional
MediaExtension reader registry also does not exist, so
MTRegisterProfessionalVideoWorkflowFormatReaders has no success placeholder.
This framework is staged only and does not make QuickTime load or play.

The staged QuickTime compatibility build also exports the documented **void**
MTRegisterProfessionalVideoWorkflowFormatReaders opt-in. There is still no
MediaExtension reader registry: it emits an unavailable diagnostic once and
registers nothing. This is a loadable absence path, not a sixth implemented
media utility. Five operations are real, one reports unavailable and the seventh
(FigAssetCreateWithURL) remains absent because its callable ABI is unestablished.
See ../QuickTime/ABI.md and the actual public
[void declaration](https://developer.apple.com/documentation/MediaToolbox/MTRegisterProfessionalVideoWorkflowFormatReaders()?language=objc).

The new provider passes the existing native-matched math/name programs again
on the M1: 52 and 211 stdout lines, identical stdout/stderr and identical test
binary SHA-256 values. See
`runs/sffinch/quicktime-mt-math-stage-v1/comparison.json`. The private-provider
contract separately calls the professional-reader opt-in twice and observes
exactly one unavailable diagnostic. These checks do not establish the unknown
FigAsset callable contract or introduce a MediaExtension registry.
