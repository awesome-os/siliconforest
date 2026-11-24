# SPDX-License-Identifier: MIT
# Preserve the exact native-publication variant used by qualified CIO29.
let
  base = import ../m1-native-input/build-hid-rev002.nix;
  inherit (import ../rebase/pd.nix) pkgs;
in base.overrideAttrs (old: {
  pname = "siliconforest-firefox-native-hid-readiness";
  # Original manifest pre/post checks and publication patch run first.
  postPatch = (old.postPatch or "") + "\n" + ''
    python3 ${../m1-hid-select/prepare.py} "$PWD"
  '';
  nativeBuildInputs = (old.nativeBuildInputs or []) ++ [ pkgs.python3 ];
  installPhase = (old.installPhase or "") + ''
    dest="$out/nix-support/source/src/Kernel/Extensions/IOHIDFamily"
    mkdir -p "$dest/IOHIDSystem" "$dest/IOHIDFamily"
    cp src/Kernel/Extensions/IOHIDFamily/IOHIDSystem/SFHIDEventQueue.{cpp,h} "$dest/IOHIDSystem/"
    cp src/Kernel/Extensions/IOHIDFamily/IOHIDFamily/IOHIDEventService.cpp "$dest/IOHIDFamily/"
  '';
})
