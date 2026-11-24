# Staged VideoToolbox compression and CPU pixel sessions

This combines whole Finch VTCompression.c, VTPixelSessions.c, the common session
code and VTH264.cpp with the retained SFAppleAVD decoder. OpenH2642.6.0 is built
from source on Linux/Nix (BSD notices shipped). Finch MIT/Apache notices remain.
No Mac build, Apple SDK header or downloaded firmware is used.

The port uses PD's real CFRuntime ABI. Shared VTSession property operations
route by CF type ID: existing decoder objects keep the existing implementation;
encoder/pixel objects use Finch's implementation. The unused Finch decoder kind
cannot be instantiated. Existing constant values and decoder source behavior
are retained; only absent constant symbols are added. Compression inputs are
bounded to8192x8192 before upstream integer products/allocations. C++ compilation
uses PD's libc++ headers and its CF enum/_Bool declaration compatibility flags.

Build `build.nix` and `tests.nix`. The public-API probe generates30 NV12 frames
at1280x720, requests no frame reordering and software selection, sets8Mbit/s,
30fps, GOP30, writes our pattern and Annex-B H264, checks ordered callbacks,
SPS/PPS and bounded AVCC sample lengths. It reports whether the hardware query
is supported; missing native property support is never called hardware=false.

Actual M1 prefix evidence (vt-encoder-stage-v2):30 ordered callbacks, hardware
query=false,42227 encoded bytes,3.43fps,7.940519 own-process CPU seconds,
8.744980 wall seconds. Unmodified ffmpeg bottle, no DYLD overrides, decoded all
30 frames and measured58.873634dB average PSNR against the generated source.
This measures an ordinary desktop session and the whole API/pixel-conversion
path, not an isolated OpenH264 core benchmark. Generic RGB-mediated pixel
conversion is slow; no realtime guarantee.

Same-byte Linux-built M5 public-API reference:30 ordered callbacks,36637bytes,
software requested. Native returns-12900 for the hardware-use query, so actual
native hardware/software selection is unknown. Native fps/CPU are recorded but
are not an M1 hardware-vs-software comparison. Only program stdout/stderr was
returned; its generated files were deleted with its fresh /tmp directory.
The first native probe queried hardware use before encoding and failed;
that receipt remains alongside the corrected one.

No AVE hardware encoding has occurred. RequireHardware currently returns the
actual unavailable error; no hardware capability is fabricated. The /dev/sfave hardware backend is paused by owner2026-11-15; the installed
software path remains independent.
This provider is now INSTALLED after the staged native/target key comparison,
1200 HW/SW decode PTS/hash matches, then the cold one-framework transaction.
All83 old exports remain,172total; four new deinterlace/field constants match
native M5. Requested field/deinterlace processing is explicitly unsupported,
not ignored. Three copied files were hashed before activation, .pre-finch is
retained, and600 hardware pictures, WAV/AAC HAL12000frames each, software
compression, desktop/dock/coreaudiod, Terminal/Activity Monitor open-a and
compat18/18 all pass. Main cold SHA256:
`dba1d23e6510187f9b8f6b1d6b9a2bf63362e6b91b9158bd8f0c79b2360f2ea9`.
No visual/acoustic or AVE hardware-encode claim.

Evidence: runs/sffinch/vt-encoder-stage-v2/{candidate,quality}.json,
m5-vt-encoder-v2-reference.json; provider pinned by the compiled probe's actual
derivation input (provider.json). Source and generated video are ours.

Installed evidence: runs/sffinch/install-VideoToolbox-v2/{qualified,regressions}.json;
vt-encoder-stage-v3/qualification.json, m5-vt-keys-v1-reference.json.

VLC progressive-field update (2026-11-15): VLC's decoder requests
Temporal/DeinterlaceFields even for ordinary progressive H264. SFSPS.h
classifies bounded SPS data. Only frame-only coding permits an identity;
unknown modes, unknown/interlaced SPS and formats with an unproven parameter
set fail. All SPS must qualify (maximum64 total parameter sets), and a later
interlaced software AVFrame reports an error. No deinterlacer is fabricated.

Host ASan/UBSan includes progressive/interlaced/short/null cases and50k bounded
fuzz inputs. Actual vendor VLC headless used its videotoolbox module for two
60-frame H264 runs: SFAppleAVD completed120 jobs, zero errors/timeouts/DART
faults, all contexts released; HAL consumed103424 frames in the open-a run.
This proves hardware work, not GUI presentation or unique VLC statistics.
Four300-frame H264/HEVC hardware/software runs then matched all1200 baseline
PTS, dimensions and MD5 values. Six M1 option cases pass: progressive identity,
interlaced/unknown rejection, software selection, require hardware and
contradictory native flags. Export audit retains172/172 symbols.

Existing exported key strings remain unchanged. Additional native literal
Require/Enable keys match the same Linux-built M5 observations, including
Require=true plus Enable=false returning-12906. Native software/progressive
sessions reject the hardware-use query with-12900; the reference probe records
that as unknown, never hardware=false. The required-hardware native case
reports hardware=true. The M1 strict probe requires a supported hardware-use
query. No AVE action is involved.

Evidence: runs/sffinch/vlc-player-hardware-v2,
vt-progressive-regressions-v1/qualification.json,
vlc-compat-stage-v5/candidate.json, m5-vt-options-v2-results.json.
The canonical progressive update is now installed, with the cold source hash
59871037fea0f33bf3ed99b523024ef4fb753f081ff2b8c4d09bf97e73f42b29.
The first update rolled back on an installer classification bug; the second
rolled back at a102.596ms wall/queue end-offset threshold. Both failures and
rollbacks remain recorded. Controlled baseline/candidate runs distinguish fixed
HAL producer lead from rate drift, documented in ../../tests/movie/README.md.
The third packet passes all22 programs, desktop/app-open checks and compat18/18;
its30-second movie has900 matching frames,1440000 audio frames and0.104ms
relative clock drift, with-86.291ms fixed end offset still reported. All frame,
hardware, HAL and rate checks remain required. Receipt:
runs/sffinch/install-VideoToolbox-progressive-v3/qualified.json. The earlier
receipt above describes the previous software-compression version.
