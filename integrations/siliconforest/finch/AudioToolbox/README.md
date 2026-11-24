# AudioToolbox file/converter providers

The extended variant is [codecs.nix](codecs.nix), documented in
[codec/README.md](codec/README.md): M4A/MP3 demux, AAC/MP3/ALAC decoding through
Linux/Nix FFmpeg, and queues through a distinct retained-PD image. Its M1 WAV/AAC
CLI completes HAL buffers. The description below applies to the earlier PCM-only
build.nix. The complete install.nix family is installed with retained PD output;
the earlier PCM-only build must stay staged. Browser AAC and real AUHAL device-ID
updates were installed09:33:26Z after prefix and full Root regressions.

Compile full AudioFile.c, AudioFormat.c, AudioFormatTables.c, AudioConverter.c,
ExtendedAudioFile.c from Finch2d9b1bb in Linux/Nix using our declarations. No queue,
unit, HAL, MIDI or output-device implementation is included. The66-export library
retains only15/78 exports of live AudioToolbox.pd: never replace the installed
AudioToolbox with this slice. Keep retained CoreAudio/HAL/coreaudiod together.

Original AudioConverter and ExtAudioFile tests MATCH identical Linux-built M5
references. Own probe MATCH:128 PCM16 frames in WAVE/AIFF/CAF, header magic,
round-trip bytes, seek64 and float conversion. Test files stay inside the unique
packet working directory and the probe removes its files. No sound is played.

Output integration: Finch AudioQueueStart returns InvalidDevice and its output
unit start/render returns NoConnection. Retain PD AudioQueue.c's IOProc callback
registration/start/stop and PD AudioUnit.c's input/output IOProc paths through
AudioDeviceCreateIOProcID/AudioDeviceStart/AudioDeviceStop. These use CoreAudio's
AudioDeviceIO.c/SFAudioServer ring transport to coreaudiod; wiring file conversion
alone does not provide a device. Any future merge must preserve all63 retained
symbols missing here and avoid mixed private queue/unit/converter objects.
