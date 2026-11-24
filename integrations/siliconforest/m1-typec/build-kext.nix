# SPDX-License-Identifier: MIT
let
  source = import ../rebase/pd.nix;
  inherit (source) pd pkgs;
  copied = pkgs.runCommand "siliconforest-typec-source" {} ''
    mkdir -p "$out"
    cp ${./kernel}/* "$out/"
    # Existing, hardware-used Linux PA Semi port; no second implementation.
    cp ${../m1-sound/kernel/sf_i2c.cpp} "$out/sf_i2c.cpp"
    cp ${../m1-sound/kernel/sf_i2c.h} "$out/sf_i2c.h"
    cp ${../m1-sound/kernel/sf_soc.cpp} "$out/sf_soc.cpp"
    cp ${../m1-sound/kernel/sf_soc.h} "$out/sf_soc.h"
    cp ${./NOTICE} "$out/NOTICE"
  '';
in pd.packages.x86_64-linux.kexts-arm64.overrideAttrs (old: {
  pname = "siliconforest-typec";
  src = pkgs.runCommand "siliconforest-typec-tree" {} ''
    cp -R ${old.src} "$out"
    chmod -R u+w "$out"
    test ! -e "$out/src/Kernel/Extensions/SFAppleTypeC"
    cp -R ${copied} "$out/src/Kernel/Extensions/SFAppleTypeC"
    printf '\nadd_subdirectory(SFAppleTypeC)\n' >> "$out/src/Kernel/Extensions/CMakeLists.txt"
  '';
  buildPhase = ''
    runHook preBuild
    ninja -C build-nix SFAppleTypeC.kext
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    bundle=build-nix/src/Kernel/Extensions/SFAppleTypeC/SFAppleTypeC.kext
    test -f "$bundle/Contents/MacOS/SFAppleTypeC"
    mkdir -p "$out/System/Library/Extensions" "$out/nix-support"
    cp -R "$bundle" "$out/System/Library/Extensions/"
    cp ${./NOTICE} "$out/System/Library/Extensions/SFAppleTypeC.kext/Contents/NOTICE"
    runHook postInstall
  '';
})
