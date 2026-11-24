# SPDX-License-Identifier: MIT
# Adopt CODEX-FX's tested four-class augmentation without forking its CGL/YUV
# implementation or replacing the retained Finch asset/capture family.
let
 e=import ../../environment.nix;
 fx=import ../../../firefox/avfoundation.nix;
 patched=fx.overrideAttrs(old: assert e.pkgs.lib.hasInfix
  "-Wl,-install_name,/System/Library/Frameworks/AVFoundation.framework/Versions/A/AVFoundation.sfcompat" old.buildCommand; {
  name="siliconforest-avfoundation-browser-classes-arm64";
  buildCommand=builtins.replaceStrings
   ["-Wl,-install_name,/System/Library/Frameworks/AVFoundation.framework/Versions/A/AVFoundation.sfcompat"]
   ["-Wl,-compatibility_version,1 -Wl,-current_version,2 -Wl,-install_name,/System/Library/Frameworks/AVFoundation.framework/Versions/A/AVFoundation"]
   old.buildCommand;
 });
in e.pkgs.runCommand "siliconforest-avfoundation-complete-arm64" {} ''
 mkdir -p $out/System/Library
 cp -R ${patched}/Frameworks $out/System/Library/
 chmod -R u+w $out
 F=$out/System/Library/Frameworks/AVFoundation.framework/Versions/A
 mkdir -p $F/Headers
 cp ${../../AVFoundation/include}/AVFoundation/* $F/Headers/
 cp ${./capture.h} $F/Headers/AVCaptureCLI.h
 cp ${./AVBrowserSurface.h} $F/Headers/AVBrowserSurface.h
 ln -s Versions/Current/Headers $out/System/Library/Frameworks/AVFoundation.framework/Headers
 # A prefix must find its own retained provider without DYLD_LIBRARY_PATH.
 ${e.p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-install_name_tool \
  -id @loader_path/AVFoundation.pd $F/AVFoundation.pd
 ${e.p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-install_name_tool \
  -change /System/Library/Frameworks/AVFoundation.framework/Versions/A/AVFoundation.pd \
  @loader_path/AVFoundation.pd $F/AVFoundation
 for image in $F/AVFoundation $F/AVFoundation.pd; do
  ${e.pkgs.python3}/bin/python3 ${../../../m1-mesa-asahi/tools/cs-rehash.py} "$image"
 done
''
