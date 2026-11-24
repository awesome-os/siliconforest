# AVFoundation assets (staged candidate only)

Thirteen whole Finch2d9b1bb implementation files: AVFMovie,AVAsset,AVAssetReader,
AVAssetWriter,AVMetadataItem,AVMetadataTables,AVFConstants,AVFTypes,AVTime,
AVTimedMetadataGroup,AVMediaSelection,AVSampleCursor,AVAssetWriterInputGroup.
The video-composition output class is excluded from AVAssetReader.m. No player,
capture,composition/rendering layer,output device or hardware encoder is included.
Requests to decode H264/HEVC/AAC or encode those codecs remain explicit errors in Finch; this
slice can demux/mux compressed samples without decoding or re-encoding them.

Linux/Nix build.nix compiles against our declarations,staged CM/CV/AudioToolbox/
AVFAudio and retained public PD CoreGraphics/UTType. Canonical version2/compat1;
re-export only the staged AVFAudio slice. Retained Foundation locale/scanner/file
methods replace equivalent newer spelling. A private CFDateFormatter adapter
supplies ISO8601 metadata conversion while Foundation lacks NSISO8601DateFormatter.
NSValue copying uses getValue: only after the source's exact type-encoding check.
No Apple SDK/header/resource or Mac build is involved.

Build tests.nix and generate fixtures with ../media/generate-fixtures.sh. The
public avasset-probe reports tracks,geometry,duration and ten video cursor entries
(offset,size,DTS,PTS,duration,sync),reads compressed H264,and muxes a new one-track
file with exactly the same payload hash. Zero-sample edit/timing markers are
released rather than treated as media payload. It creates only its named new
output file in the unique packet directory. There is no device or codec request.

Both Linux-built native M5 MP4/MOV runs exit0. All20 cursor entries match Linux
ffprobe including offsets/timestamps; both mux round-trips retain all2134 H264
bytes (FNV64 ca36548e184c8c1c for the recorded synthetic fixture). Both M1 programs now exit0 and their complete stdout MATCH the identical Linux-built
M5 programs. Candidate tables also match Linux ffprobe. No framework is installed.
The existing repository CoreMedia/VideoToolbox and PD audio paths remain intact.

prepare.py creates fresh trial/reference packets from Nix outputs; optional
--ut-provider stages only retained PD UniformTypeIdentifiers,never the package's
other frameworks. The reference packet contains only our Linux-built program,
synthetic fixtures and harness. Apply the owner-approved M5 SHA256/30s/core0/tmp
cleanup rules and return only program output. compare-tables.py checks candidate
or native receipts against Linux ffprobe JSON. Raw comparisons require identical
program hashes and remain separate from these structural assertions.

Pending: external inspection of generated candidate mux files;native
and candidate date-metadata regressions;JPEG/raw/PCM conversion tests;fragmented
or hostile media;remaining AVFoundation APIs,codecs and output integration.
