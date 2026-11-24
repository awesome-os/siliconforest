# SPDX-License-Identifier: MIT
# Private selected-device PD AU variant. Do not install generated CoreAudio.
let e=import ../environment.nix;base=import ../AudioToolbox/units/build.nix;in
base.overrideAttrs(old:{
 pname="siliconforest-pd-au-normalized-output-clock";
 src=e.pkgs.runCommand "siliconforest-pd-au-normalized-clock-source" {nativeBuildInputs=[e.pkgs.python3];} ''
  cp -R ${old.src} $out
  chmod -R u+w $out
  cp ${./output-clock.h} $out/SFOutputClock.h
  python3 ${./patch-output-clock.py} $out/AudioUnit.c
 '';
})
