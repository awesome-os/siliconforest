# Installed media evidence (15 November 2026)

Later updates: [canonical opt-in Metal](../Metal/README.md) and
[realtime AVFAudio](../AVFAudio/engine/realtime-README.md) are now installed and
qualified. Metal resolves the plain-root bottle gap below; realtime output
resolves the older AVAudioEngine limitation. Their separate cold hashes and
receipts supersede the earlier AVFAudio row while preserving this first pass.

Nine reviewed providers were installed one at a time on the M1. Each main-image
hash below comes from the target receipt, verified while cold before public
activation/execution. Existing files remain adjacent as `.pre-finch`; a zero
before-count means the canonical main path was absent, not that repository
implementations had no exports. Loaded-image readback is not pristine-byte
verification because SiliconForest can lower PAC instructions to NOPs.

| Provider | Before → after effective exports | Cold main-image SHA256 |
|---|---:|---|
| CoreVideo | 97 → 414 | `dbe7b0d8e0ffd490f08627985092c2a7c51988be82a5750507ea144be8934017` |
| CoreMedia | 0 → 2609 | `5c65d7645e93e5c7776e69b263d3a23424512f3fcc24dc298c96fc3efe6845dc` |
| VideoToolbox | 0 → 83 | `0900514c6062b45bcfbcc76c5ab9f88ffa04b1a48bf6c8e51036f6b5749f62fa` |
| AudioToolbox | 84 → 135 | `262c02fcc13114f42c6131e3a018dc7c3e7ff3f4363fb4555bd34d17309bf7a7` |
| AVFAudio | 0 → 334 | `3e62ad7d5fd1bde1a4517c0be7fb09b118c9f2e8833dc9c7fb078d861c918aa9` |
| AVFoundation | 0 → 2482 | `d08d3a07cebd189f95de450a9aa9d9312ba91d7d828654be5fda88232558272c` |
| ColorSync | 11 → 293 | `222c111540068474124ca13baaab9e37c4255bc2edab1c1a6c4464347d7fbe1c` |
| Accelerate | 0 → 3478 | `e406e824676ff60e39cf8e116d2163618d01e9912e6d46a31988c4466008ccc4` |
| CoreImage | 0 → 456 | `6372b0d2714826e7dd9cb1316a76b288fb12016e82fe35fa6583f25fc29d4975` |

CoreVideo uses the exact PD display-link implementation/private objects.
AudioToolbox keeps all six System Sound APIs and forwards every queue operation
to the retained PD image beside it. AVFAudio supports files/converters and the
offline engine; real-time AVAudioEngine output remains explicitly unavailable.
AVFoundation supplies assets/reader/writer and truthful no-device capture
objects, not AVPlayer. CoreImage is the legacy-class re-export facade with zero
new CI class definitions, not the conflicting whole Finch CPU implementation.

Every transaction passed root-provider lookup, 300 H.264 and 300 HEVC hardware
PTS/pixel comparisons, WAV/AAC buffers through retained HAL/coreaudiod, desktop
process health, fresh `open -a` Terminal/Activity Monitor, and compatibility
18/18. Framework-specific probes also match their previously qualified M1
stages (native differences are retained, not reclassified as equivalence).
The installed AVFoundation test requires all 900 native movie PTS/hashes and
its hardware/HAL/clock bounds. Final app opens passed after all nine installs
and the later compositor relaunches; no input or screenshot was taken here.
A read-only replay after the USB02 kernel reboot at 02:46:26Z again passed
`/dev/sfavd` existence, all nine provider lookups, 600 hardware pictures,
WAV/AAC HAL and fresh app opens (`final-media-health-usb02-v1.json`).

## Hardware decode

The installed-media Homebrew ffmpeg comparison passes 600/600 H.264/HEVC 720p
B-frame pictures, bit-exact against software, Linux and identical Linux-built
M5 controls. Single end-to-end hardware/software trials, including startup and
hashing under normal desktop load: H.264 267.38/184.96 fps; HEVC 268.82/149.93 fps.
These are not isolated decoder benchmarks. Earlier fully staged trials were
179.64/141.64 and 120.97/77.42 fps before the core-library performance update.

The zero-DYLD bottle trial is a retained FAIL: canonical Metal does not export
`MTLCreateSystemDefaultDevice`, required at load by libavfilter. The passing
trial overrides **Metal only**, with the previously tested real nil-device
implementation; all nine media providers resolve from the installed root.
No Metal rendering/filter claim or Metal installation is made. GFX2 has the
exact public-path request. Direct installed VT hardware clients also pass
without that Metal override.

## Thirty-second movie

Our Linux/Nix Objective-C application uses AVAssetReader→VT and
ExtAudioFile→AudioQueue→retained HAL, scheduling against one audio sample clock.
The original hosted-CALayer test exited 0 but reported unavailable libEGL, so its
900 layer submissions were not called rendered video. The corrected own frame
layer uses a CPU NSView drawing its CGImage through CoreGraphics, without
NSView hosted-layer rendering. All 900 actual draw callbacks, ordered PTS and
decoded hashes match Linux and the same Linux-built M5 executable.

At the restored 150% desktop scale: hardware=1, 900/900 draw callbacks, 1,440,000
source frames queued/returned at 48 kHz, 1,407 callbacks. Scheduling A/V offset
starts at 0 and ends 6.666 ms late; maximum/mean delivery lateness 73.333/9.935 ms.
Wall 29.978961 s versus audio-clock 29.999979 s (-21.018 ms). Queue clock ends at
1,439,999 frames, one short of the returned source-buffer count. Drawing and
queue counters are not compositor/DAC presentation acknowledgements. TERM5 has
the exact command and remains responsible for visual/panel verification.

## Retained failures and external work

- Two early CoreVideo transaction/launcher failures rolled back; the third
  qualified. AVFAudio trial 1 rolled back because the probe was given an absolute
  argument despite appending its input to cwd; corrected invocation passes
  with unchanged provider/program bytes.
- ColorSync trial 1 rolled back when public `CC_MD5` returned NULL and left the
  output untouched after the core update. PERF has the direct two-vector
  diagnostic and owns the core fix. ICC profile checksums now use the existing
  MIT m1-avd MD5 locally; 21 ASan/UBSan host vectors and all 152 M1/native result
  lines pass. This does not repair or conceal the public CommonCrypto failure.
- Whole vImage has one retained native DIFF: Display P3 coarse result 80 vs 82
  (1,158/1,159 output lines match). No general numeric-equivalence claim.
- MediaAccessibility stays staged: default types/ranges pass, positive PNG
  caption persistence fails because ImageIO does not persist XMP. Our patch
  refuses before replacing the original; the negative unchanged-PNG test
  passes. Precise ImageIO and single-CI-class-provider requests are with TERM5.

All six required repository checks pass, alongside 20 coordination-gate and
six cold-installer host cases. The movie classifier also rejects missing draw
records and an altered picture hash when replaying the captured run.

Ignored evidence: `runs/sffinch/installed-media-ledger-v1.json`, each
`install-*/{receipt,regressions,qualified}.json` and `compat.log`,
`hardware-root-v1/candidate.json` (plain-root failure),
`hardware-root-metal-only-v2/qualified.json`,
`movie-window-cpu-v3-qualified.json`, `m5-movie-cpu-v3-reference.json`,
`ccmd5-core-diagnostic.*`, and `ma-stage-v{1,2}/candidate.json`. Earlier
controllers lack a separate qualified.json; their CLI/pass logs and exact
compatibility records remain. Raw logs are not committed. No Mac build, SDK
import, kernel build, firmware action, push, input injection or screenshot.
