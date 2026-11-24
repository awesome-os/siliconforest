# SPDX-License-Identifier: MIT
let
  e = import ../environment.nix;
  cm = import ../CoreMedia/build.nix;
  cv = import ../CoreVideo/build.nix;
in e.pkgs.runCommand "siliconforest-finch-media-declarations" {} ''
  mkdir -p $out
  cp -r ${../CoreMedia/include}/* $out/
  chmod -R u+w $out
  cp -r ${../CoreVideo/include}/* $out/
  chmod -R u+w $out
  cp ${cm}/System/Library/Frameworks/CoreMedia.framework/Versions/A/Headers/Generated.h $out/CoreMedia/
  cp ${cv}/System/Library/Frameworks/CoreVideo.framework/Versions/A/Headers/Generated.h $out/CoreVideo/
''
