# Compressed files, conversion and retained PD output

`../codecs.nix` extends the five pinned Finch AudioFile/AudioFormat/tables/
AudioConverter/ExtendedAudioFile implementation files. Original provenance stays
in the pinned source and patch. The FFmpeg bridge and output adapter are ours.

The usable decoder dependency is the **Linux/Nix-built LGPL FFmpeg8.1.2** from
`../ffmpeg.nix`, not Homebrew's libraries. The shared recipe already enables AAC,
MP3 and MOV/MP3 demux;this variant adds ALAC. Static codec/demux libraries are
private inside AudioToolbox,with their implementation exports hidden. No Apple
SDK header,Mac build or Apple codec binary is used. The exact FFmpeg LGPL2.1
notice and license explanation are packaged under Resources/Licenses/FFmpeg;
the Nix source input and build recipe provide the matching source/relink path.

AudioFile retains WAVE/AIFF/AIFC/CAF/NeXT reading/writing and adds read-only
M4A/MP3 demux via libavformat's callback I/O. It exposes the original compressed
format,packet descriptions,codec cookie,valid length/priming/padding and packet
reads. Encoded payload is buffered (256MiB input/payload and1M-packet limits);
this is not an unbounded streaming implementation. File creation/encoding of
AAC/MP3/ALAC is unsupported. Compressed CAF/cross-provider cookie interoperability
is unverified;the measured compressed containers are M4A and MP3.

AudioConverter keeps Finch PCM/G.711/sample-rate behavior and adds AAC/MP3/ALAC
packet decoding through libavcodec,followed by its existing PCM conversion path.
The complex-input API consumes one encoded packet at a time,validates descriptor
ranges and keeps pending decoded frames. The fixed-size PCM conversion calls
reject compressed input. Cookies are set before first decode;raw ASC and ALAC
24/36-byte forms are supported. ExtAudioFile supplies file cookies,skips priming,
limits output to valid frames and implements compressed seek by reset/redecode/
discard. Seek cost is proportional to position. General streaming,encoding,
codec switching,multichannel layouts and malformed-input coverage remain limited.

Output uses **the retained SiliconForest queue implementation**,not Finch's offline
queue internals. prepare-codecs.py checks the retained image's complete reviewed
78-symbol export set,stages its exact implementation as AudioToolbox.output-sf,
changes only its install name/ad-hoc hashes,and records before/after SHA256.
The distinct identity is necessary:opening the installed .pd with the canonical
identity under a framework override resolved back to the candidate. Every queue
operation goes to the distinct retained provider. Its private PCM converter,
queue,AudioUnit/AUGraph family stay together;no opaque object is passed into the
Finch queue/converter family. The candidate also re-exports retained APIs. The
.tbd is our symbol-facts interface to this existing PD code,not a success stub
or a copied SDK. PD source provenance:AudioQueue.c and related AudioToolbox files,
Copyright2026 PureDarwin,MPL-2.0 OR BSD-2-Clause. No installed image is changed.

M1 final audio-codecs-v7 (after the retained core-library update):all9 programs exit0. Original converter,ExtAudioFile and PCM/file probe
match identical Linux-built native M5 programs. WAV and ALAC each decode12000
frames and seek64 identically to M5;lossless PCM hashes also match Linux ffmpeg.
AAC/MP3 produce the correct12000 valid frames and seek positions,with small
native sample/RMS/hash differences retained as DIFF. Original AudioConverter's
only raw difference was the printed provider path (the standard first-line skip
is used in the final manifest). Initial compressed-converter and provider-load
failures remain in receipts;they were not turned into passes.

The afplay-style `sf-afplay` decodes before queueing,then uses the existing
CoreAudio/HAL/coreaudiod path. WAV and AAC each queued/completed12000 input
frames through12 callbacks,error0;queue time query succeeds(sample time11999).
This is HAL consumption evidence,not a listener or hardware-emission claim.
Its volume is a local queue parameter;no global device setting,server,kernel or
installed framework was changed. M5 reference packets exclude the output tool.

Build `../codecs-tests.nix`,generate original fixtures with fixtures.sh NEW_DIR,
then use ../prepare-codecs.py with explicit Nix outputs,retained-PD image and new
stage/reference destinations. The final v7 regression includes the full retained
family re-export and hidden FFmpeg implementation exports (86 direct public
exports plus the retained provider;all78 previous PD names available). The earlier v4/v6
receipts are retained separately. Hash-check the reference packet on M5,explicitly
name its reviewed file/converter tests to the comparison runner,and enforce
the fresh /tmp,30s/core0,no-DYLD,stdout/stderr-only,cleanup reference procedure.

## Browser AAC and real AUHAL device selection candidate, 15 November

Three source fixes proven by CODEX-FX's private Firefox H264/AAC and HEVC/AAC
runs are included in the owned candidate: float PCM with bits matching its
container may omit the redundant Packed flag; FormatList rejects an incomplete
zero-rate/channel ASBD rather than overwriting a caller's valid description;
AAC cookies accept bounded MPEG-4 ESDS/DecoderSpecificInfo and raw ASC before
passing configuration to the real FFmpeg decoder. No non-AAC cookie is changed.
The parser's raw/wrapped/truncation/length/20000 bounded fuzz checks pass ASan
and UBSan. Original fix provenance: firefox/media-patches0001/2/3 and its owned
MIT aac-cookie.h; no Apple SDK/header/binary input.

The candidate also stages the PD private provider from ../units, retaining all78
reviewed exports (80total) with validated per-unit actual output selection.
Only AudioToolbox.output-sf changes in this retained family; generated CoreAudio
is not staged. The complete Finch main keeps135/135 effective prior exports.
The complete candidate is now installed after the checks below. prepare-codecs.py keeps exact reviewed exports
by default; --allow-retained-superset explicitly records added symbols for this
reviewed source port.

M1 audio-browser-stage-v1 passes11of12 tests, including original converter/file
programs, four decoders, WAV/AAC HAL completions, raw-ASC/ESDS PCM equality and
real-device AUHAL. The one failure is a harness error: SystemSound was invoked
without its required WAV filename (exit2); the corrected fresh probe passes completion1/dispose0.
Final-source M1 probe passes: raw ASC and ESDS each13312 float frames with
identical PCM hash; AUHAL again consumes12288 frames with clean lifecycle. The same Linux-built new browser
probe fails on native M5 because native AudioConverter rejects its raw5-byte ASC
cookie with560226676. This is retained as a reference difference/failure, not
claimed native-matched behavior or general AAC-cookie conformance. Existing
native-matched lossless file/PCM tests are separate evidence.

Installation09:32:16–09:33:26Z:20 cold file hashes before activation; first
.pre-finch originals plus immediate-prior snapshots retained. Main cold SHA256
4bb3b6976d27f0fe2c4130e0a2cc8ced256c63d7f45bb5cb7c1aca96150799d7.
Root selection,600 H264/HEVC hardware pictures with unchanged PTS/hashes,
WAV/AAC HAL completion, System Sound, three AVPlayerNode output lifetimes,
offline/file APIs, new browser-cookie/AUHAL probes, desktop/dock/coreaudiod,
Terminal/Activity Monitor open-a and compatibility18/18 all pass. Receipts:
runs/sffinch/install-AudioToolbox-v2/{manifest,receipt,regressions,qualified}.json.
Only the owned AudioToolbox family changed; CoreAudio/coreaudiod stayed in place.

The [15 November difference audit](../../DIFFERENCES-2026-11-15.md) measures every
S16 sample from identical Linux-built M1/M5 probes. WAV and ALAC are exact;
MP3 differs by at most one LSB. AAC's default PNS case differs by at most 5/12
LSB, while disabling encoder PNS reduces that to one LSB in just one left-channel
sample and two right-channel samples out of 12000 frames.
Frames, seeks and zero-lag alignment match. These retained differences are
consistent with permitted decoder/PNS/rounding variation for these fixtures;
this is not a normative ISO conformance qualification. The raw DIFF stays visible.
