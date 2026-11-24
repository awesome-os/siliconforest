# SPDX-License-Identifier: MIT
# FFmpeg 8.1.2 for the SiliconForest arm64 userland (./player/build-ffmpeg.nix) with the
# `sfavd` hwaccel (./ffmpeg/0001-sfavd-hwaccel.patch: hardware decode of H.264, HEVC and VP9
# through /dev/sfavd) and libsfavd (./build-user.nix) linked in. Use: `ffmpeg -hwaccel sfavd`,
# or in a program av_hwdevice_ctx_create(..., AV_HWDEVICE_TYPE_SFAVD / "sfavd", ...).
#   nix-build --extra-experimental-features nix-command --extra-experimental-features flakes \
#     --no-out-link --cores 4 --max-jobs 1 build-ffmpeg-sfavd.nix
let
  user = import ./build-user.nix;
in import ./player/build-ffmpeg.nix {
  extraPatches = [ ./ffmpeg/0001-sfavd-hwaccel.patch ];
  extraConfigureFlags = [ "--enable-sfavd" "--enable-hwaccel=h264_sfavd" "--enable-hwaccel=hevc_sfavd"
                          "--enable-hwaccel=vp9_sfavd" ];
  extraBuildInputs = [ user ];
  extraCFlags = [ "-I${user}/include/sfavd/compat" ];
  extraLibs = [ "-lsfavd" ];
  nameSuffix = "-sfavd";
}
