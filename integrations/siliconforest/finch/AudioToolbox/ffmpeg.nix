# SPDX-License-Identifier: MIT
# Linux cross build, LGPL FFmpeg libraries; no platform audio/video framework.
# Existing recipe already enables AAC/MP3, MOV/MP3 demux and PCM conversion.
import ../../m1-avd/player/build-ffmpeg.nix {
  extraConfigureFlags = [ "--enable-decoder=alac" ];
  nameSuffix = "-finch-audio";
}
