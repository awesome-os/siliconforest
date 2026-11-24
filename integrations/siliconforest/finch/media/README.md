# Media CLI staging

Linux/Nix only; no SDK headers or Mac builds. CoreMedia/CoreVideo are whole
candidates; AudioToolbox and AVFAudio are device-independent slices. Nothing is
installed. Recipes build arm64 using the existing PAC-off loading policy.

Build CoreMedia/build.nix, CoreVideo/build.nix, AudioToolbox/build.nix,
AVFAudio/build.nix, media/videotoolbox.nix, media/tests.nix,
AudioToolbox/tests.nix and AVFAudio/tests.nix. Optionally build
CoreVideo/sf-displaylink.nix as the retained-display-link reconciliation.
`generate-fixtures.sh NEW_DIR` creates synthetic MP4/MOV, ffprobe packet/stream
receipts and NV12 frame hashes with Linux ffmpeg.

`prepare.py --help` accepts those outputs, the pinned Finch source and generated
MP4 and creates a fresh candidate stage plus binaries-only reference packet.
It rewrites only our test binaries to public Foundation load names and renews
their ad-hoc hashes. Capture the candidate under a fresh sffinch path using
`compare-tests.py capture manifest.json --role candidate --output candidate.json`.
The reference packet has no trial libraries or DYLD overrides. Verify SHA256SUMS
on M5, explicitly select its reviewed manifest names for reference capture, enforce
30s per process/core0, and remove the unique tmp directory afterwards (chmod u+w
on copied read-only directories before cleanup). Only own program output returns.

`inventory.py BEFORE CANDIDATE --client CLIENT --output JSON` records hashes,
export differences and exact CM/CV imports. Export retention is necessary but
not an ABI or runtime proof. sf_gapscan.py requires a complete retained-root
snapshot with the candidate overlay searched first.

Evidence: media-cli-v1 on M1 all11 exit0; ten same-byte native M5 references all
exit0. Seven MATCH; CM/CV and AVAudioBuffer remain DIFF. VideoToolbox raw DIFF
includes hardware availability/timing; its10 sample PTS/dimensions/NV12 MD5 tuples
match M5 and all10 hashes match Linux ffmpeg. No hardware decode claim. Separate
PD-display-link candidate also completes all3 CPU tests, with no GUI interaction.

`numeric-facts.py` extracts numeric values from maintainer objc2 Rust0.3.2 bindings
as reviewed public facts; it never consumes an SDK. `declarations.py` derives C
prototypes from preprocessed Finch definitions; own headers provide ABI types.
Upstream licenses/copyrights are preserved in pinned sources and patch context.
