# SPDX-License-Identifier: MIT
# sfvideo built natively for this Linux workstation, for testing the player's logic (queues,
# sync, window, statistics) quickly under a headless Wayland compositor. Same source; the audio
# output is the clock-paced stand-in (there is no AudioQueue here). The FFmpeg is nixpkgs' full
# build, so --hwdec=vaapi and the like exercise the generic hwaccel path.
#   nix-build ... build-sfvideo-linux.nix
let
  pkgs = (import ../../rebase/pd.nix).pkgs;
  proto = "${pkgs.wayland-protocols}/share/wayland-protocols";
in pkgs.stdenv.mkDerivation {
  name = "darwin-lab-sfvideo-linux";
  src = ./src;
  nativeBuildInputs = [ pkgs.wayland-scanner pkgs.pkg-config ];
  buildInputs = [ pkgs.ffmpeg pkgs.wayland pkgs.libGL ];
  buildPhase = ''
    for x in stable/xdg-shell/xdg-shell stable/viewporter/viewporter staging/fractional-scale/fractional-scale-v1 \
             unstable/xdg-decoration/xdg-decoration-unstable-v1; do
      n=$(basename $x)
      wayland-scanner client-header ${proto}/$x.xml $n-client-protocol.h
      wayland-scanner private-code ${proto}/$x.xml $n-protocol.c
    done
    $CC -std=gnu11 -O2 -g -Wall -Wextra -Wno-deprecated-declarations -DSFVIDEO_GL -I. sfvideo.c *-protocol.c \
      $(pkg-config --cflags --libs libavformat libavcodec libswresample libswscale libavutil wayland-client wayland-egl egl glesv2) -lm -lpthread -o sfvideo
  '';
  installPhase = "install -Dm755 sfvideo $out/bin/sfvideo";
}
