# Builds the MIT SFGpuDT.kext (T8103 GPU device-tree synthesis) in the pinned SiliconForest tree.
#   nix-build --extra-experimental-features nix-command --extra-experimental-features flakes build-kext.nix
let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  copied = pkgs.runCommand "siliconforest-gpu-dt-source" {} ''
    mkdir -p "$out"
    cp ${./kernel/CMakeLists.txt} "$out/CMakeLists.txt"
    cp ${./kernel/Info.plist} "$out/Info.plist"
    cp ${./kernel/SFGpuDT.cpp} "$out/SFGpuDT.cpp"
    cp ${./kernel/SFGpuDT.h} "$out/SFGpuDT.h"
    cp ${./src/gpudt.c} "$out/gpudt.c"
    cp ${./src/gpudt.h} "$out/gpudt.h"
    cp ${./src/gpudt_summary.c} "$out/gpudt_summary.c"
    cp ${./src/gpudt_fw_table.inc} "$out/gpudt_fw_table.inc"
    cp ${./kernel/SFDispDT.cpp} "$out/SFDispDT.cpp"
    cp ${./src/dispdt.c} "$out/dispdt.c"
    cp ${./src/dispdt.h} "$out/dispdt.h"
  '';
in pd.packages.x86_64-linux.kexts-arm64.overrideAttrs (old: {
  pname = "siliconforest-gpu-dt";
  src = pkgs.runCommand "siliconforest-gpu-dt-tree" {} ''
    cp -R ${old.src} "$out"
    chmod -R u+w "$out"
    test ! -e "$out/src/Kernel/Extensions/SFGpuDT"
    cp -R ${copied} "$out/src/Kernel/Extensions/SFGpuDT"
    printf '\nadd_subdirectory(SFGpuDT)\n' >> "$out/src/Kernel/Extensions/CMakeLists.txt"
  '';
  buildPhase = ''
    runHook preBuild
    ninja -C build-nix SFGpuDT.kext
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    bundle=build-nix/src/Kernel/Extensions/SFGpuDT/SFGpuDT.kext
    test -f "$bundle/Contents/MacOS/SFGpuDT"
    mkdir -p "$out/System/Library/Extensions"
    cp -R "$bundle" "$out/System/Library/Extensions/"
    runHook postInstall
  '';
})
