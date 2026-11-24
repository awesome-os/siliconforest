# SPDX-License-Identifier: MIT
# Builds SFAppleNVMeRW.kext (GPL-2.0: the WRITE-FENCED build of the internal NVMe driver of
# T8103) in the SiliconForest tree. Sources: ./kernel with ./kernel-rw/rw.patch applied, plus
# ./kernel-rw (tools/rw_src.sh). Never part of a default collection: only
# build-kc-rw.nix / build-kc-native-rw.nix name it.
#   nix-build --extra-experimental-features nix-command --extra-experimental-features flakes build-kext-rw.nix
let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  copied = pkgs.runCommand "siliconforest-apple-nvme-rw-source" { nativeBuildInputs = [ pkgs.patch ]; } ''
    mkdir -p "$out"
    cp ${./kernel}/*.c ${./kernel}/*.cpp ${./kernel}/*.h "$out/"
    cp ${./kernel-rw}/CMakeLists.txt ${./kernel-rw}/Info.plist ${./kernel-rw}/sf_nvme_wring.c ${./kernel-rw}/sf_nvme_wring.h "$out/"
    chmod -R u+w "$out"
    patch -p1 -d "$out" < ${./kernel-rw/rw.patch}
  '';
in pd.packages.x86_64-linux.kexts-arm64.overrideAttrs (old: {
  pname = "siliconforest-apple-nvme-rw";
  src = pkgs.runCommand "siliconforest-apple-nvme-rw-tree" {} ''
    cp -R ${old.src} "$out"
    chmod -R u+w "$out"
    test ! -e "$out/src/Kernel/Extensions/SFAppleNVMeRW"
    cp -R ${copied} "$out/src/Kernel/Extensions/SFAppleNVMeRW"
    printf '\nadd_subdirectory(SFAppleNVMeRW)\n' >> "$out/src/Kernel/Extensions/CMakeLists.txt"
  '';
  buildPhase = ''
    runHook preBuild
    ninja -j6 -C build-nix SFAppleNVMeRW.kext
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    bundle=build-nix/src/Kernel/Extensions/SFAppleNVMeRW/SFAppleNVMeRW.kext
    test -f "$bundle/Contents/MacOS/SFAppleNVMeRW"
    mkdir -p "$out/System/Library/Extensions"
    cp -R "$bundle" "$out/System/Library/Extensions/"
    runHook postInstall
  '';
})
