# SPDX-License-Identifier: MIT
# Private PD provider source port. Never stage/install its generated CoreAudio.
let
 e=import ../../environment.nix;
 base=(import ../../../m1-sound/sysaudio/build-sysaudio.nix).audiotoolbox;
in base.overrideAttrs(old:{
 pname="siliconforest-pd-audio-unit-device-selection";
 src=e.pkgs.runCommand "siliconforest-pd-audio-unit-device-source" {nativeBuildInputs=[e.pkgs.python3];} ''
  cp -R ${old.src} $out
  chmod -R u+w $out
  python3 ${./patch-unit.py} $out/AudioUnit.c
 '';
})
