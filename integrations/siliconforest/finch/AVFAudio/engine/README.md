# AVFAudio files, compressed conversion and engine

The installed [realtime variant](realtime-README.md) now connects this graph to
the retained PD AudioQueue/HAL path. `build.nix` remains the old offline-only
recipe so earlier receipts can be reproduced. Its NoConnection behavior below
describes that older recipe, not the installed realtime provider.

Stage-only variant of the six value/file/converter sources plus the whole
AVAudioEngine.m graph/player/mixer/source/sink implementation. Own declarations
cover19 public classes;numeric/type facts come from Finch implementations and
public maintainer objc2 bindings0.3.2,never Apple SDK headers. Links the staged
AudioToolbox codec/retained-PD output variant and packed CoreMedia ABI.

The converter patch accepts AAC/MP3/ALAC compressed input blocks,retains pending
packet buffers,validates offsets/sizes and forwards codec cookies to the actual
C decoder. PCM conversion remains the original implementation. AVAudioFile
reads through the extended ExtAudioFile bridge. The CLI probe separates file
reading from raw converter packets:ALAC file reading succeeds on native M5,
while its raw-converter case currently fails there with OSStatus560226676.
That reference failure is preserved;it is not ALAC converter conformance.
AAC/MP3 file and raw-converter cases and WAV file reading pass on M5.
Final `avaudio-engine-v3` (after the retained core-library update) runs all8 cases on M1 with exit0. The original engine,
WAV and ALAC files MATCH identical Linux-built M5 programs; AAC/MP3 numerical
differences remain DIFF. The raw ALAC comparison remains FAIL because the native
reference fails, despite the staged decoder completing it. Final recipe rebuilds
produce the same tested framework and portable program bytes.

The original269-line offline engine program builds in Linux/Nix and runs on
M1 and same-byte native M5. It covers scheduling/looping/completions,times,
mixing/pan,connections,source nodes,taps and manual-render errors. Both exit0;
stdout comparison and any differences must remain visible. This is an offline
graph result,not proof of realtime output or every node/API.

Real-time output is deliberately explicit:the upstream timer rendered and
threw samples away while returning success. The patch returns NoConnection
instead. The remaining wiring point is startAndReturnError:'s non-manual branch:
feed _renderCycle:'s planar float buffers to the retained PD queue adapter,
interleave/convert via AudioToolbox,and synchronize stop/disposal with callbacks.
Use the HAL callback clock rather than the old dropping timer. The M1 C afplay
client already exercises that queue path,but it does not make AVAudioEngine's
realtime branch wired. AudioUnit/voice/MIDI/session/recorder/spatial processing,
realtime graph changes and suspend/lifetime stress remain unverified.

M5 runs are file/offline only,with the exact Linux-built bytes,SHA256 checks,
30s timeout,no DYLD overrides,fresh removed temporary directories. All generated
media is original;no Apple files,SDK or builds are copied back.

Build tests.nix and build.nix, then use prepare.py with those Nix outputs,
an AudioToolbox/prepare-codecs.py stage (including its distinct retained image),
CoreMedia/CoreVideo outputs and fresh stage/reference destinations. The reference
packet contains only own programs, synthetic fixtures and the comparison runner.
After remote hash verification, explicitly name the reviewed tests to
`compare-tests.py capture manifest.json --role reference --output reference.json`.
Use a new M5 /tmp directory,30s/core0,no DYLD overrides,and remove it afterwards;
bring back only the program/runner stdout/stderr,not decoded files.
