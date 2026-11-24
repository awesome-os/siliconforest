# SPDX-License-Identifier: MIT
# Notices from the exact open-source FFmpeg input used by ffmpeg.nix.
let e=import ../environment.nix;
in e.pkgs.stdenvNoCC.mkDerivation {
  name="siliconforest-finch-ffmpeg-notices";
  src=e.pkgs.ffmpeg.src;
  dontConfigure=true;dontBuild=true;
  installPhase=''
    mkdir -p $out
    cp COPYING.LGPLv2.1 LICENSE.md $out/
  '';
  dontFixup=true;
}
