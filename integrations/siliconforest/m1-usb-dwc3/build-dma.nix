# SPDX-License-Identifier: MIT
# SDK compilation only; no USB host service or collection member yet.
let
  source = import ../rebase/pd.nix;
  inherit (source) pd pkgs;
  copied = pkgs.runCommand "siliconforest-usb-dma-source" {} ''
    mkdir -p "$out"
    cp ${./kernel}/* "$out/"
    cp ${../m1-dcp-dart/include/sf_dart_table.h} "$out/sf_dart_table.h"
    cp ${../m1-dcp-dart/src/sf_dart_table.c} "$out/sf_dart_table.c"
    cp ${../m1-sound/kernel/sf_soc.cpp} "$out/sf_soc.cpp"
    cp ${../m1-sound/kernel/sf_soc.h} "$out/sf_soc.h"
  '';
in pd.packages.x86_64-linux.kexts-arm64.overrideAttrs (old: {
  pname = "siliconforest-usb-dma-objects";
  src = pkgs.runCommand "siliconforest-usb-dma-tree" {} ''
    cp -R ${old.src} "$out"
    chmod -R u+w "$out"
    cp -R ${copied} "$out/src/Kernel/Extensions/SFUSBDma"
    printf '\nadd_subdirectory(SFUSBDma)\n' >> "$out/src/Kernel/Extensions/CMakeLists.txt"
  '';
  buildPhase = ''
    runHook preBuild
    ninja -C build-nix SFUSBDma
    runHook postBuild
  '';
  installPhase = ''
    mkdir -p "$out/objects"
    cp build-nix/src/Kernel/Extensions/SFUSBDma/CMakeFiles/SFUSBDma.dir/*.o "$out/objects/"
    cp ${./NOTICE} "$out/NOTICE"
  '';
})
