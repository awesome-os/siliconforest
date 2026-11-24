# SPDX-License-Identifier: MIT
# Isolated IOHIDFamily variant; never selected by the default collection.
let
 source=import ../rebase/pd.nix;
 inherit (source) pkgs pd;
in pd.packages.x86_64-linux.kexts-arm64.overrideAttrs (old: {
 pname="siliconforest-hid-select";
 src=pkgs.runCommand "siliconforest-hid-select-tree" {nativeBuildInputs=[pkgs.python3];} ''
  cp -R ${old.src} "$out"
  chmod -R u+w "$out"
  python3 ${./prepare.py} "$out"
 '';
 buildPhase=''
  runHook preBuild
  ninja -C build-nix IOHIDFamily.kext
  runHook postBuild
 '';
 installPhase=''
  mkdir -p "$out/System/Library/Extensions"
  cp -R build-nix/src/Kernel/Extensions/IOHIDFamily/IOHIDFamily.kext "$out/System/Library/Extensions/"
 '';
})
