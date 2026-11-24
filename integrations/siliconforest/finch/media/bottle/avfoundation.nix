# SPDX-License-Identifier: MIT
# Asset core + real no-device discovery/output value objects, no GUI/capture driver.
let
  e=import ../../environment.nix;
  base=import ../../AVFoundation/build.nix;
in base.overrideAttrs(old:{
  name="siliconforest-finch-AVFoundation-bottle-capture-absence-arm64";
  nativeBuildInputs=(old.nativeBuildInputs or []) ++ [e.pkgs.python3];
  buildPhase=builtins.replaceStrings
    [ "${e.cc} -dynamiclib objects/*.o" ]
    [ ''
        python3 ${./capture.py} userland/AVFoundation/AVCapture.m CaptureCLI.m
        cp ${./capture.h} capture.h
        ${e.cc} $CF -I$PWD -c CaptureCLI.m -o objects/CaptureCLI.o
        ${e.cc} -dynamiclib objects/*.o'' ] old.buildPhase;
})
