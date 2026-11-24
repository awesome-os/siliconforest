# SPDX-License-Identifier: MIT
# Builds SFAppleNVMe.kext (GPL-2.0: READ-ONLY driver for the internal NVMe storage of T8103) in the
# pinned SiliconForest tree. Sources: ./kernel.
#   nix-build --extra-experimental-features nix-command --extra-experimental-features flakes build-kext.nix
let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  copied = pkgs.runCommand "siliconforest-apple-nvme-source" {} ''
    mkdir -p "$out"
    cp ${./kernel}/* "$out/"
  '';
in pd.packages.x86_64-linux.kexts-arm64.overrideAttrs (old: {
  pname = "siliconforest-apple-nvme";
  src = pkgs.runCommand "siliconforest-apple-nvme-tree" {} ''
    cp -R ${old.src} "$out"
    chmod -R u+w "$out"
    test ! -e "$out/src/Kernel/Extensions/SFAppleNVMe"
    cp -R ${copied} "$out/src/Kernel/Extensions/SFAppleNVMe"
    printf '\nadd_subdirectory(SFAppleNVMe)\n' >> "$out/src/Kernel/Extensions/CMakeLists.txt"
  '';
  buildPhase = ''
    runHook preBuild
    ninja -C build-nix SFAppleNVMe.kext
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    bundle=build-nix/src/Kernel/Extensions/SFAppleNVMe/SFAppleNVMe.kext
    test -f "$bundle/Contents/MacOS/SFAppleNVMe"
    mkdir -p "$out/System/Library/Extensions"
    cp -R "$bundle" "$out/System/Library/Extensions/"
    runHook postInstall
  '';
})
