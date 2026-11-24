# Movie integration test

`movie.m` is our Objective-C test application, built on Linux by `build.nix`
using SiliconForest and our declarations. `fixtures.sh NEW_DIRECTORY` generates an
original 30-second H.264/AAC MOV and Linux NV12 frame hashes with FFmpeg.
`prepare.py --help` describes the isolated framework/reference packets. Rewrite
our test's public load names with `tools/portable-tests.py` before reference use;
run the identical bytes on both machines. Never build on the Mac or import an
Apple SDK.

The program reads compressed AVAssetReader samples into VideoToolbox and AAC
into ExtAudioFile. It retains bounded decoded buffers (512 MiB video, 64 MiB
audio), sorts decoded pictures by PTS, and then schedules them against the
retained SiliconForest AudioQueue sample clock. Its AudioToolbox provider forwards
queue operations to the existing HAL/coreaudiod path. This is a finite test,
not a streaming player implementation or an AVPlayer class port.

Modes:

- `--decode-only`: hashes and timestamps only; no output device or window.
  This is the permitted native-reference mode, with a 30-second timeout.
- `--headless`: sends decoded audio to AudioQueue and logs ordered video
  deliveries against that audio clock for 30 seconds.
- `--window`: creates our AppKit window. Our frame CALayer holds each CGImage;
  an own CPU NSView draws it synchronously into the window CGContext. Each
  frame requires its actual `drawRect:` callback before continuing. The
  `MOVIE_VIEW_DRAW` records prove this drawing path, not compositor scanout.
  No NSView hosted-layer/CGL renderer is needed.

`--software` disables the hardware preference. Every mode reports whether
VideoToolbox actually used hardware; preference alone is insufficient evidence.

On the installed M1 with `/dev/sfavd`, the second staged run passed all 900
hardware-decoded pictures and 1,440,000 stereo audio frames at 48 kHz. Every
ordered PTS/hash matched both Linux FFmpeg and the same Linux-built executable
on the M5. Audio completion took 29.943529 seconds, with an audio clock of
29.999979 seconds, a clock/wall difference of -56.450 ms, and maximum/mean video
delivery lateness of 60.000/9.887 ms. Video delivery relative to the audio clock
starts at 0 ms and ends 17.333 ms late: 17.333 ms of A/V delivery drift across
the run. This differs from the audio-clock/wall measurement above. These are
one-fixture headless measurements;
they establish neither acoustic output nor visible scanout. The window check
has been requested from TERM5. The later CPU window evidence is below.

The queued/completed audio counts describe source buffers returned by the PD
HAL IOProc. The reported queue sample clock ends at 1,439,999 frames (one frame
short of the source count); neither counter is a DAC presentation acknowledgement.

The first M1 run failed before audio start: the AVAssetReader returned raw
composition timestamps including codec delay. Patch
`AVFoundation/patches/0006-full-track-video-edit-offset.patch` handles a full,
untrimmed video track's single unity-rate edit by mapping its first composition
time to asset time. It does not implement arbitrary trims, repeated edits or
rate changes. The failing receipt remains alongside the passing receipt.

Evidence (ignored): `runs/sffinch/movie-stage-v{1,2}/candidate.json`,
`movie-stage-v2/qualified.json`, `m5-movie-v1-reference.json`. Reproduce the
qualification with:

```sh
python3 integrations/siliconforest/finch/tests/movie/report.py \
  runs/sffinch/movie-stage-v2/candidate.json \
  runs/sffinch/m5-movie-v1-reference.json \
  runs/sffinch/movie-fixtures-v1/movie.nv12.framemd5 \
  --output runs/sffinch/movie-stage-v2/qualified.json
```

The original hosted-CALayer window trial exited 0 but reported unavailable
libEGL, so its 900 contents submissions were not called displayed video. The
CPU frame-layer path removes that dependency. On the installed root at 150%
scale, all 900 actual view draw callbacks and decoded PTS/hashes match Linux
and the identical new Linux-built M5 executable. The 30 s run uses hardware
decode, returns 1,440,000 source frames at 48 kHz, and reports scheduling A/V drift
6.666 ms, maximum/mean delivery lateness 73.333/9.935 ms, and audio-clock/wall
difference -21.018 ms. Offsets use the scheduling audio-clock tick; the view
callback and flush are not compositor/DAC presentation acknowledgements.
There was no synthetic input or screen capture.

`prepare.py --installed-root` produces just the program/app/fixture packet,
with no private framework inputs or DYLD overrides. It reproduces the tested
program bytes. Staged mode still accepts the explicit framework/audio inputs.
Native reference mode is decode-only and never opens a window or output device.
The latest window qualification is:

```sh
python3 integrations/siliconforest/finch/tests/movie/report.py \
  runs/sffinch/movie-window-cpu-v3-combined.json \
  runs/sffinch/m5-movie-cpu-v3-reference.json \
  runs/sffinch/movie-fixtures-v1/movie.nv12.framemd5 --window \
  --output runs/sffinch/movie-window-cpu-v3-qualified.json
```


Clock instrumentation update (2026-11-15): the old clock_wall_drift_ms field
was an end-to-end wall/queue offset, including startup and HAL producer lead.
It is retained for old receipts, with the explicit alias clock_wall_offset_ms.
New probes also report clock_relative_drift_ms: change in mean wall-minus-queue
offset between the 5-10% and 90-95% audio-clock windows, with both sample counts.
This measures rate stability after startup, separately from A/V delivery lateness.
It does not measure output latency, acoustic sound or physical display sync.

A VideoToolbox update rolled back when the old offset reached-102.596ms despite
all900 frames and1440000 audio frames completing. A controlled unchanged-
provider/candidate/unchanged-provider run retained offsets-98.329/-102.623/
-95.901ms. With explicit early/late instrumentation, installed/candidate relative
drift was-0.365/-0.010ms (fixed end offsets-86.285/-81.810ms). Both900-frame
PTS/hash sequences match the current Linux-built M5 decode reference exactly.
Evidence: runs/sffinch/movie-clock-ab-v{1,2} and m5-locale-movie-v1-results.json.
The installer opts into the new protocol only for the expected hashed new
program and requires nonempty early/late windows and drift below100ms;
frame order/hashes, hardware, all HAL buffers and rate checks remain required.
