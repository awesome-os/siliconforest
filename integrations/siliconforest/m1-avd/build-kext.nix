# SPDX-License-Identifier: MIT
# Builds SFAppleAVD.kext (GPL-2.0: the Apple Video Decoder of T8103 as /dev/sfavd) in the pinned
# SiliconForest tree. Sources: ./kernel (with the unchanged Linux files in ./kernel/linux) and
# ./include/sfavd (the ioctl interface and Linux's v4l2-controls.h).
#   nix-build --extra-experimental-features nix-command --extra-experimental-features flakes build-kext.nix
let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  copied = pkgs.runCommand "siliconforest-apple-avd-source" {} ''
    mkdir -p "$out"
    cp -R ${./kernel} "$out/kernel"
    cp -R ${./include} "$out/include"
  '';
in pd.packages.x86_64-linux.kexts-arm64.overrideAttrs (old: {
  pname = "siliconforest-apple-avd";
  src = pkgs.runCommand "siliconforest-apple-avd-tree" {} ''
    cp -R ${old.src} "$out"
    chmod -R u+w "$out"
    test ! -e "$out/src/Kernel/Extensions/SFAppleAVD"
    cp -R ${copied} "$out/src/Kernel/Extensions/SFAppleAVD"
    chmod -R u+w "$out/src/Kernel/Extensions/SFAppleAVD"
    printf '\nadd_subdirectory(SFAppleAVD/kernel)\n' >> "$out/src/Kernel/Extensions/CMakeLists.txt"
  '';
  buildPhase = ''
    runHook preBuild
    ninja -C build-nix SFAppleAVD.kext
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    bundle=build-nix/src/Kernel/Extensions/SFAppleAVD/kernel/SFAppleAVD.kext
    test -f "$bundle/Contents/MacOS/SFAppleAVD"
    mkdir -p "$out/System/Library/Extensions"
    cp -R "$bundle" "$out/System/Library/Extensions/"
    runHook postInstall
  '';
})
