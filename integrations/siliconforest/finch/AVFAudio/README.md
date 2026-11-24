# AVFAudio file/converter and engine variants

The installed [realtime variant](engine/realtime-README.md) now wires engine
output to retained PD AudioQueue/HAL. The original source slices below remain
available for earlier receipts; their output limitations are historical.

The extended [engine variant](engine/README.md) adds the complete offline graph
source and compressed file/converter input. Its M1 offline engine,WAV and ALAC
file results match same-byte M5 references; realtime engine output still returns
NoConnection pending retained-PD queue wiring. The description below applies to
the earlier six-file build.nix.

Six whole implementation files: AVAudioTime.m, AVAudioFormat.m, AVAudioBuffer.m,
AVAudioConverter.m, AVAudioFile.m, AVFConstants.m. Eight value/file/converter
classes, own source-derived declarations, MRC and real CoreMedia format descriptions.
Link staged AudioToolbox/CoreMedia. No engine, node, player, recorder, session,
AudioQueue or output-unit implementation is included. Engine output needs retained
PD HAL/coreaudiod integration; see ../AudioToolbox/README.md.

Original compressed-buffer/converter/file tests MATCH same-byte native M5 outputs;
all4 original programs exit0 on M1. Buffer expected comparison DIFF only at
malformed keyed-archive test: truncated-layout-rejected=1,0 versus1,1. Settings
reject truncated layout and ordinary secure archive round-trip passes; malformed
archive construction/validation through retained Foundation is still unresolved.
No installation or general audio-engine support claim.
