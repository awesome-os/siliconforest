# Homebrew ffmpeg software milestone (staged only)

The arm64 Tahoe ffmpeg9.0.2_1 bottle **runs on the M1** in the fresh
`/private/var/db/sffinch/ffmpeg-bottle-v3` stage. Software decode of our synthetic
MP4 produces10 NV12 framemd5 records,all matching Linux and the Linux-built
native-M5 control. All4 staged programs exit0:the bottle,the Nix control,the
capability/provider probe,and the retained VideoToolbox software decoder.
Nothing existing is installed or replaced;no kernel is built or booted.

`prepare.py` copies the ordinary Homebrew closure from an sf_gapscan CLOSURE
log,rewrites only Homebrew dependency/install names and ad-hoc hashes,and records
before/after SHA256. System imports are retained. It accepts the three media
outputs plus explicit `--provider NAME=OUTPUT` entries and optional own CLI
probes. Stage destinations must be new. `imports.py` reports actual Mach-O load
ordinals,bound symbols and weak flags,not matches in source text.

## Providers selected for this software command

| Provider | Staged implementation and observed scope |
|---|---|
| CoreMedia/CoreVideo | The packed whole Finch candidates already independently tested;all requested bottle video symbols are available. No root/compositor replacement. |
| VideoToolbox | Existing PD AVD/FFmpeg decoder rebuilt against those declarations. `videotoolbox.nix` adds pinned Finch constants and explicit unavailable optional encoder/pixel-session responses. Creation fails with NULL;no encoder,pixel transfer/rotation or GPU success is fabricated. Professional registration adds no codecs. |
| AudioToolbox/AVFAudio | Tested codec/retained-PD queue and offline-engine stages. Exact retained output-provider image stays beside AudioToolbox. PD AudioUnit/AUGraph objects remain within that provider;no existing audio stack is replaced. |
| AVFoundation | `avfoundation.nix` extends the tested asset core with selected actual non-GUI Finch AVCapture.m bodies:nine classes including base and screen input. Device/discovery enumerate empty,device input fails. Source session start is corrected to remain stopped and post DeviceNotConnected. Own screen initializers return nil because no screen backend exists. No preview,photo,file-output or capture driver is claimed. |
| CoreImage | `coreimage.nix` compiles Finch constants only and **re-exports the real retained QuartzCore CI classes**,using our five-class public-symbol TAPI facts. It defines zero Objective-C classes,so the full Finch CPU class family is never loaded alongside the retained one. M1 dlsym/runtime identity assertions pass5/5. This is a load facade;CoreImage filters in ffmpeg are untested. The separate whole Finch CPU port still needs owner reconciliation before integration. |
| Metal | Existing `cocoa/build-own-frameworks.nix`,unmodified. Its real nil-device implementation supplies MTLCreateSystemDefaultDevice while preserving observer functions. The M1 assertion observes nil;this does not provide Metal rendering or compute. |
| UniformTypeIdentifiers/AppKit/QuartzCore | Previously tested PD UTType staged for AVFoundation;installed AppKit and QuartzCore retained. NSOpenGLPixelFormat exists. No GUI operation,AppKit/QuartzCore source edit or version rewrite. |

The no-device discovery/notification/output value objects are source ports,not
successful capture stand-ins. The M1 probe observes devices0,discovery0,running0,
screen input nil,and Metal device nil. There is no working camera,microphone,
screen capture,Metal,CoreImage filter or optional VT encoder claim.

## Evidence and reproduction

Build the three media recipes,AudioToolbox/codecs.nix,AVFAudio/engine/build.nix,
this directory's avfoundation.nix/coreimage.nix/tests.nix,and the existing Cocoa
own-framework recipe. Use ../generate-fixtures.sh and AudioToolbox's fresh stage
preparer to obtain original media and the distinct retained-PD image. Supply
these explicit provider outputs to prepare.py,including the tested PD UTType
framework if it is absent on the selected root. Its manifest fixes30s per CLI,
software decoding and private framework overrides. No test requests capture
permission or starts a session/device/GPU.

The actual bottle invocation is:

```
ffmpeg-bottle -nostdin -v error -hwaccel none -threads 1 \
  -i fixtures/test-av.mp4 -map 0:v:0 -an -pix_fmt nv12 -f framemd5 -
```

Receipts under runs/sffinch/ffmpeg-bottle-v3:manifest.json,candidate.json,
provider-hashes.json,bottle-relocation.json,frame-comparison.json and
coreimage-provider-audit.json. All10 frame records equal the Nix8.1.2 control
and the native-M5 run of that same Linux-built control. The Homebrew bottle is
9.0.2 and is **not** claimed to be the same program as the reference control.
The retained `vt-decode-test --software` separately reports10 samples/pictures,
errors0,hardware0;all10 NV12 hashes match. Its earlier timing/capability raw
comparison differences remain differences.

The M5 control packet used identical Linux-built executable bytes,remote SHA256
verification,a fresh removed /tmp directory,no DYLD overrides/build/install,
30s/core0,and stdout/stderr-only return. Native receipt:
m5-ffmpeg-software-v1-reference.json. The earlier ffmpeg-bottle-v2 missing-AVFoundation
load failure is retained,not reclassified. New staged providers resolve it.
A subsequent sf_gapscan snapshot audit still lists missing cached graphics
libraries and two Foundation/CF symbols;the local root snapshot is partial/stale,
so no clean static closure or general app ABI claim is made. The actual M1
four-program capture is the execution evidence for this exact command.

## Hardware AVD: enabled and observed on the disk system

../audit-avd-config.py reads only a local SiliconForest record list,validates
CRCs,and enumerates actual MH_FILESET members. The14 November baseline disk collection
already contained org.siliconforest.driver.SFAppleAVD;its config lacked sfavd=1.
A filtered M1 read at that time also observed no sfavd argument/node.
SFAppleAVD::start returns early for absent/zero sfavd. The exact activation-only
request is in runs/m1-coord/requests-for-VK.md;no kernel was built,installed or
booted by this stream. First open additionally needs the separate **open-source
MIT AVD** firmware image,not Apple's AVE firmware. Hardware decode remains open.

Update15 November2026: GFX2 installed sfavd=1 and the open-source AVD image.
The live device/flag/firmware are now present. `hardware-decode-v1` uses the
staged media family with **the exact retained-PD-display-link CV variant**,
and the actual disk kernel/device. H.264 and HEVC300-frame720p30 B-frame clips
both complete through the Homebrew bottle with provider `hardware1`; every
framemd5 record matches the bottle's software mode, Linux, and the Linux-built
Nix control on M5. The Linux-built VT client also matches native M5's sorted
PTS/dimensions/pixel hashes for all600 frames. Native reference packets are
removed. Process-wall throughput (startup+hash output,normal desktop load,
single trial) is179.6fps H.264 versus141.6software and121.0fps HEVC versus77.4.
The actual required-hardware VT summaries report300 pictures/errors0/hardware1
for each; callbacks are decode order,so consumers must reorder by PTS.
Use hardware-fixtures.sh and hardware-report.py in the parent media directory.
Receipts/qualification live in runs/sffinch/hardware-decode-v1/qualified.json
and m5-hardware-media-v1-reference.json. The earlier absent-node finding above
is historical,not the current decoder state. Framework installation has a
separate per-provider cold-hash/backup/regression workflow;this hardware result
alone does not claim an installed-framework swap or a GUI regression pass.
