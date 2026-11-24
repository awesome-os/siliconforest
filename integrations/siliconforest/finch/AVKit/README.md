# Finch AVKit, prefix stage

**Parked 2026-11-15 by owner direction:** Firefox media takes priority. Keep
all existing source, stages, raw failures and installed providers. The bounded
controller and native resolver/menu probes remain for resumption. No further
AVKit/QuickTime/VLC launch is queued; unresolved behavior is not implemented
merely to fill symbol names.

Build `build.nix` and `tests.nix` with the Linux/Nix toolchain. This ports the
whole Finch `userland/AVKit/AVKit.m` against our own declarations and the staged
Finch player sources in `../AVFoundation/player`. No Apple SDK input.

The Linux-built value probe matched native M5 and M1 output: player assignment,
control-style defaults, route-picker properties and PiP support query. The
M1 reports no OpenGL context and uses ordinary view fallback; this test proves
no video rendering, audio, fullscreen presentation or picture-in-picture.
The first M1 probe overlapped a newly announced TERM6 idle measurement; this
was disclosed. Its functional result is retained, not uncontended timing.
A fresh07:26Z rerun under gui.lock repeats the functional pass; neither run
establishes rendering or a timing benchmark.

The original value-stage used Finch's clock/JPEG model. The new private player
variant below adds real bounded local-file VideoToolbox/AudioQueue playback.
It remains staged and does not replace installed AVFoundation. PiP explicitly
reports unsupported.

QuickTime's prior qualified inventory has 6/18 AVKit imports provided: AVPlayerView
class/metaclass, AVAssetIdentifier, AVTimecode and the two slow-motion range
conversion functions. The actual export audit is
`runs/sffinch/quicktime-avkit-mediatoolbox-stage-audit-v2.json`.

The range functions use CoreMedia rational arithmetic and propagate invalid
inputs. Expanded M5 vectors matched M1 exactly, including zero, invalid,
negative-start and rounding cases. `PRIVATE-ABI.md` records the local call-site
layouts; `math-reference.{c,nix}` supplies our independently built observations.

`values.m` adds immutable asset-identity snapshots and timecode metadata.
Existing file identifiers compare device/inode identity, including symlinks;
missing paths have URL identity. CoreFoundation cannot create file-reference
URLs here, so assetURL is a working canonical path URL. It cannot follow later
renames like native file-reference URLs. This limitation is handed to the core
owner rather than hidden. AVTimecode independently stores frame number,
duration, flags and SMPTE fields, matching the native setters; it does not
invent frame-number-to-SMPTE conversion. The constructor accepts four colon-
or semicolon-separated nonnegative components within signed16-bit bounds.
The native-reference vectors do not establish every private method.

After the audio-level addition below, the then-remaining 11 imports were the two
media-selection-menu functions, AVCaptureView (+ metaclass), AVPlayerController,
AVPlayerControllerTimeResolver, AVPlayerControlsViewController (+ metaclass),
AVTouchBarCaptureInputSourceItem, AVTouchBarRecordingControlsProvider and
AVVolumeControlSlider. Native runtime method signatures alone do not supply
these classes' behavior. No name-only placeholder is supplied.

MediaToolbox now provides six of its seven reviewed imports, documented in
`../MediaToolbox/README.md`. Both frameworks remain staged. In particular,
The new bounded player has a real H264/AAC output path, but the remaining
QuickTime imports and untested private behavior still prevent a QuickTime
playback claim.

Evidence: `runs/sffinch/avkit-stage-v1/`, `media-math-stage-v1/comparison.json`,
`avkit-value-stage-v1/candidate.json`, and the same-byte value comparison in
`avkit-value-stage-v2/` / `m5-avkit-values-v5-results.json`.


## Real local-file player and our transport drawing

`player-stage.nix` packages AVKit with the coherent full AVFoundation prefix from
`../AVFoundation/player/complete.nix`. `player-view.py` retains Finch's AVKit
model and adds our play/pause shapes, progress bar, click handlers and CPU layer
fallback. Frame images come from AVPlayerLayer, backed by actual decoded media.
The UI uses our shapes rather than unavailable image names. Mouse interaction
is implemented but was not driven during the qualification.

Generate our own5-second/150-frame H264+AAC fixture with
`generate-player-fixture.sh NEW_DIRECTORY`, build `player-stage.nix`, and call
`prepare-player.py --package OUTPUT --movie FIXTURE/movie.mov --stage NEW_STAGE`.
Run `run-player.py NEW_STAGE` only under gui.lock then tools/bench.sh; it checks
the coordination gate, uploads to a fresh owned directory and records actual
AVD counters. No input, window, capture, restart or installed change occurs.
`player-report.py` compares every decoded NV12 timestamp/hash to the independent
Linux FFmpeg framemd5, preserving failures.

M1 `avkit-real-player-stage-v3` PASS:150/150 decoded images, all150 PTS/MD5 values
match, AVD+150done/errors0/contexts0,240000 decoded audio frames, actual48kHz
output, pause holds and seek0.2s succeeds,230400 post-seek frames returned/error0.
The real layer reaches frame149/ready.279 offscreen CPU view/control draws alter
27840 video-center pixels versus a black reference; that is not279 presented
frames. Native M5 value output matches the same Linux-built binary. No window,
compositor, panel, acoustic or full QuickTime playback claim follows.

Backend bounds and unsupported modes are in `../AVFoundation/player/README.md`.
Evidence: `runs/sffinch/avkit-real-player-stage-v3/{qualified,hardware,native-values,export-audit}.json`.
The earlier stage-v1/v2 passes remain separately. Final source also checks
missing-file failure without false playback capability, preserves runtime output
errors and resets the layer on item changes.

`SFAudioLevel.m` adds a real AVAudioLevelIndicatorView with our normalized
24-segment CPU meter. The caller's doubleValue is preserved without clipping;
only drawing clamps to [0,1], and no audio/capture measurement is invented.
A Linux-built metadata/property probe on native M5 observes initial0, all eight
signed/out-of-range values preserved, intrinsic size(-1,9), and vibrancy true.
Our property implementation follows those observations. `level-draw-probe.nix`
checks changed bitmap pixels between quiet/half/full levels without a window.
M1 media-private-controls-v1 now matches the same-byte native property output
exactly; quiet-to-half and half-to-full drawings change2916/2754 bitmap bytes.
It adds the seventh reviewed QuickTime import, with no window, QuickTime launch
or native pixel-style claim. The61 prior exports remain in the64-export image.

# Additional capture-input value model

`SFCaptureInputItem.m` implements the private data item imported by QuickTime,
with measured title/flag/represented-value ownership and equality. It provides
no capture or touch-bar service. See
[the bounded reference audit](CAPTURE-ITEM-2026-11-15.md); the added import raises
source coverage to8/18; its same-byte M1 comparison matches native. Existing player
and transport code is unchanged; all64 old exports are retained.


## Capture view explicitly unavailable

`SFUnavailableCaptureView.m` adds the observed NSView subclass and initializer
ABIs, but every initializer returns nil with one diagnostic per process. There
is no capture preview or recording backend. This permits class lookup while
making creation fail; it does not implement QuickTime recording. The M1-only
probe deliberately never runs the native capture UI on the controller Mac.

M1 `avkit-capture-unavailable-stage-v1` at20:58:27–29Z returned nil for all three
initializers without a crash; the capture-input value probe still matched every
raw native-reference row. The private image preserves71 previous exports and
adds the capture class/metaclass,73 total. Thus10/18 reviewed imports resolve,
but only8 have implemented behavior and the two capture-view symbols explicitly
report unavailable. The eight unresolved imports are the two media-selection
menu functions, AVPlayerController, AVPlayerControllerTimeResolver,
AVPlayerControlsViewController (+metaclass), AVTouchBarRecordingControlsProvider
and AVVolumeControlSlider. No canonical AVKit installation or GUI test occurred.


## Bounded transport controller and current strict failure

`SFPlayerController.m` provides real scalar/transport operations over our
AVPlayer. [Its audit](CONTROLLER-2026-11-15.md) separates the three exact native/
M1 value comparisons and successful transport/HAL assertions from the current
strict playback failure:149/150 frame hashes match under two other decoder
contexts, with a new global stale completion. Frame40 is wrong; this is not a
bit-exact pass. No tolerance or classifier was weakened. The private image has
77 exports, all73 prior retained;11/18 reviewed imports resolve (9 implemented,
2 explicitly unavailable). Private UI/KVO/inspection families remain incomplete.
