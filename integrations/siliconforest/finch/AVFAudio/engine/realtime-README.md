# Engine output through retained PD queues

`realtime.nix` extends the whole Finch graph with our `0003` patch and
`sf-engine-output.h`. CoreAudio's actual default-output nominal rate is queried
through its public property API. A missing device/property remains an error.
The graph produces planar float stereo, interleaved into three 1,024-frame queue
buffers. Returned buffers pull the next graph cycle; no timer discards audio.
The installed AudioToolbox forwards those queues wholly into the retained PD
provider, whose HAL IOProc sends buffers to coreaudiod.

Stop/pause marks the context stopping, stops/disposes the queue, and joins its
callback thread before releasing the PCM buffer/context. Startup errors clean
up the same way. The old offline recipe remains unchanged. Our diagnostic
`sf_outputStatistics` reports queued/returned frames, nonzero samples, callbacks,
nominal rate and the independent queue sample clock; it is not an Apple API or
a DAC presentation acknowledgement. Call start/stop from a control thread, not
from a render/tap callback. Concurrent live graph mutation, sample-accurate
pause/resume and arbitrary device/channel changes are not qualified here.

Installed M1 at 05:41:02Z on 15 November 2026, preserving 334/334 exports.
Twelve cold hashes were checked before activation; originals `.pre-finch`.
Main SHA256: `a3f9919515a07ccce41b21858162759dbca46d37b9927f3b74ad17ce1d35e427`.
AVAudioPlayerNode playing our 12,000-frame stereo WAV passed three independent
start/stop/deallocation lifetimes at the actual 48 kHz device rate. Every run
reported 23,940 nonzero source samples, zero errors and HAL sample time at least
12,000. Returned buffers in the first snapshot were 11,264 frames; source-clock
consumption and callbacks are recorded separately rather than equated.

The new offline file probe matches identical Linux-built M5 bytes exactly:
12,000 frames, 48 kHz stereo, FNV 1dce8b45. Finch's original 269-line offline
comparison also matches after excluding its provider-path line. The first
native test played before starting the engine and rendered silence; its failure
is preserved, and the corrected sequence starts the offline engine before play.
Native references open no output device. M1 realtime references use real HAL.

Installed Root/VT 600 hardware frames/WAV+Aac HAL/fresh Terminal+Activity Monitor/
desktop health/compatibility18 checks pass, together with the existing AAC file
probe. No acoustic claim. Build `realtime.nix` and `realtime-tests.nix` on Linux;
portable tests use our existing `tools/portable-tests.py`. Offline `--offline`
is the native-reference mode; `--output` requires our diagnostic and real output.
Ignored evidence: `runs/sffinch/{avaudio-realtime-v2,install-AVFAudio-realtime-v1}/`,
`m5-next-media-v{1,2}-reference.json`. No Foundation/CoreAudio/coreaudiod files
were replaced by this change.
