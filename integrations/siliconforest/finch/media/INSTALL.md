# Reviewed media installation

The owner authorizes these media replacements. AppKit, QuartzCore, core
libraries and kernels remain with their owners. Security/corecrypto are not
eligible: public export coverage does not repair their private ABI mismatch.

Select the tested variant and capture the old provider's effective exports
before preparing a packet with `tools/prepare-install.py`. CoreVideo must use
`CoreVideo/sf-displaylink.nix`, retaining the exact PD display-link objects.
AudioToolbox must use `AudioToolbox/install.nix` with the exact retained output
image beside it: all queue operations stay in that image, and the existing
System Sound implementation is kept. Whole CoreImage still conflicts with
QuartzCore's CI classes; use only the tested re-export facade until the other
owner supplies a coherent single-provider change.

Dependency order is CoreVideo, CoreMedia, VideoToolbox, AudioToolbox, AVFAudio,
AVFoundation. Verify AVFoundation's UniformTypeIdentifiers dependency is already
present. ColorSync precedes whole Accelerate. MediaAccessibility additionally
requires Cocoa's real ImageIO metadata implementations and canonical CoreText.
Keep existing processes' opaque objects within their original provider.

Outside every announced core-install, reboot and risky-GPU window, run one
prepared packet under the shared locks. For example, after CoreVideo passed:

```sh
flock -n runs/m1-coord/gui.lock env BENCH_WHO=codex-finch-coremedia \
  tools/bench.sh run python3 integrations/siliconforest/finch/tools/install-media.py \
  runs/sffinch/install-CoreMedia-v1 --installed CoreVideo \
  --baseline runs/sffinch/hardware-decode-v1/candidate.json
```

List every previously installed family member using another `--installed`.
The controller excludes them from the private dependency override; dladdr then
proves root selection. Its target checks decode H.264 and HEVC through m1-avd,
require 300 hardware pictures per codec with the qualified PTS/pixels, play WAV
and AAC through retained HAL, check exact executable process names, invoke
`open -a` for Terminal and Activity Monitor with the actual desktop socket, and
require the compatibility suite's 18/18 result. `--extra-commands` accepts
reviewed JSON `{name,argv}` rows for framework-specific probes; an optional
`cwd` keeps relative-fixture programs in their own packet directory. These
receive the same explicit dependency environment and a 50-second bound.
`expected_sha256` binds a probe to previously exercised bytes, and
`stage_expected_stdout` with `skip_lines` compares it to its qualified M1
stage without asserting native equivalence. `native_movie_frames` requires all
900 ordered native-reference PTS/hashes and the hardware/HAL/clock summary.
Run the movie
headless check after the audio/AVFoundation family is installed. App-open and
process checks are not screen/scanout evidence: TERM5 owns that check and input.

The target installer copies images to hidden adjacent files, fsyncs them, and
hashes **all** cold copies before public activation. The main image activates
last. Old files stay `.pre-finch`; the receipt records exact cold hashes and
every activated path. A loaded image's subsequent readback can have PAC lowered
to NOPs and is not a pristine source-byte hash. The controller retains failures
and rolls back on failed regressions while the coordination gate permits work.
If SSH dies before activation is confirmed, inspect the target receipt first;
do not assume rollback or retry a new install blindly. A receipt-bearing packet
cannot be reused by this controller. Never push these commits or build kernels.
