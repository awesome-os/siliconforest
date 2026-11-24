# Retained PD AUHAL real device selection

VLC's actual auhal plugin selects the real default output ID272. The retained
PD AudioUnit rejected every ID except its0x40 follow-default alias, returning
-10851 before initialization. patch-unit.py ports that implementation from a
fresh PD source copy: each unit retains its selected validated alive output ID,
queries its output PCM format/buffer size and uses it for IOProc lifecycle.
The default alias remains supported; unknown IDs and changes after initialize
are rejected. Converter/queue/unit opaque objects stay in one retained image.

build.nix builds PD's family with this one AudioUnit source patch. Its generated
CoreAudio is NEVER staged or installed: the installed Root CoreAudio/HAL and
coreaudiod remain the transport. Prefix qualification copies only the private
AudioToolbox.output-sf into our existing Finch framework; its78 reviewed exports
are all retained (80total). The complete AudioToolbox family update is now installed09:33:26Z. The original
.pre-finch and separate immediate-prior main snapshot are preserved. Generated
CoreAudio was never staged or installed.

probe.c selects the real default output, rejects an unknown ID, registers its
own mono48k PCM callback and runs the public AUHAL initialize/start/stop/dispose
lifecycle. M5 reference of the same Linux-built executable passes24callbacks/
12288produced; native clock values are device-global and not claimed as this
process's consumed ring. On our Root AudioDeviceGetCurrentTime reports the
actual per-process readFrame consumed by coreaudiod; M1 prefix test passes28callbacks/14339produced and12288 HAL-consumed
frames, selected actual default ID and rejected unknown, stop/uninit/dispose0.
No acoustic or microphone claim. Evidence: runs/sffinch/au-device-stage-v1 and
m5-au-device-v1-reference.json.

Installed receipt: runs/sffinch/install-AudioToolbox-v2/{receipt,regressions,qualified}.json.
All20 file hashes were checked on M1 before activation. Private provider cold
SHA256 d2bbdfda228787a449176161a18714f009a2f6b5a0665047f50744a792fd274b.
After installation real-ID AUHAL again consumes12288frames and cleanly stops,
plus WAV/AAC queues, System Sound, AVFAudio offline/output/file APIs,600 AVD
pictures/PTS/hashes, desktop/app opens and compatibility18/18 pass. No acoustic,
microphone or arbitrary device-switching conformance claim.
