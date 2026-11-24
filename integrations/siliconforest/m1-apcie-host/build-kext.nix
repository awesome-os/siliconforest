# Builds the GPL-2.0 SFApcieT8103Bridge.kext (host bridge revision) in the pinned SiliconForest tree.
let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  copied = pkgs.runCommand "siliconforest-apcie-host-source" {} ''
    mkdir -p "$out"
    cp ${./CMakeLists.txt} "$out/CMakeLists.txt"
    cp ${./Info.plist} "$out/Info.plist"
    cp ${./SFApcieT8103.cpp} "$out/SFApcieT8103.cpp"
    cp ${./SFApcieT8103.h} "$out/SFApcieT8103.h"
    cp ${./SFApcieT8103Link.cpp} "$out/SFApcieT8103Link.cpp"
    cp ${./SFApcieT8103Link.h} "$out/SFApcieT8103Link.h"
    cp ${./SFAppleSMC.cpp} "$out/SFAppleSMC.cpp"
    cp ${./SFAppleSMC.h} "$out/SFAppleSMC.h"
    cp ${./NOTICE} "$out/NOTICE"
  '';
in pd.packages.x86_64-linux.kexts-arm64.overrideAttrs (old: {
  pname = "siliconforest-apcie-host";
  src = pkgs.runCommand "siliconforest-apcie-host-tree" {} ''
    cp -R ${old.src} "$out"
    chmod -R u+w "$out"
    test ! -e "$out/src/Kernel/Extensions/SFApcieT8103Bridge"
    cp -R ${copied} "$out/src/Kernel/Extensions/SFApcieT8103Bridge"
    printf '\nadd_subdirectory(SFApcieT8103Bridge)\n' >> "$out/src/Kernel/Extensions/CMakeLists.txt"
  '';
  buildPhase = ''
    runHook preBuild
    ninja -C build-nix SFApcieT8103Bridge.kext
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    bundle=build-nix/src/Kernel/Extensions/SFApcieT8103Bridge/SFApcieT8103Bridge.kext
    test -f "$bundle/Contents/MacOS/SFApcieT8103Bridge"
    mkdir -p "$out/System/Library/Extensions"
    cp -R "$bundle" "$out/System/Library/Extensions/"
    runHook postInstall
  '';
})
