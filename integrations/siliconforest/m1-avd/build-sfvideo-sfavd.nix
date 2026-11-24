# SPDX-License-Identifier: MIT
# sfvideo (./player/build-sfvideo.nix) linked against the FFmpeg build with the sfavd hwaccel
# (./build-ffmpeg-sfavd.nix): the player with hardware decoding. `gl = true` builds sfvideo-gl.
#   nix-build --extra-experimental-features nix-command --extra-experimental-features flakes \
#     --no-out-link --cores 4 --max-jobs 1 build-sfvideo-sfavd.nix [--arg gl true]
{ gl ? false }:
let
  user = import ./build-user.nix;
in import ./player/build-sfvideo.nix {
  ffmpeg = import ./build-ffmpeg-sfavd.nix;
  extraLdFlags = [ "-L${user}/lib" ];
  extraLibs = [ "-lsfavd" ];
  nameSuffix = "-sfavd";
  inherit gl;
}
