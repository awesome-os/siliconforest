# SPDX-License-Identifier: MIT
# Builds SFCpuPower.kext (GPL-2.0: T8103 cluster performance states that follow load, under
# a die-temperature limit read from the SMC) in the pinned SiliconForest tree. Sources: ./kernel,
# ./src and ../m1-thermal.
#   nix-build --extra-experimental-features nix-command --extra-experimental-features flakes build-kext.nix
let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  copied = pkgs.runCommand "siliconforest-cpupower-source" {} ''
    mkdir -p "$out"
    cp ${./kernel/CMakeLists.txt} "$out/CMakeLists.txt"
    cp ${./kernel/Info.plist} "$out/Info.plist"
    cp ${./kernel/SFCpuPower.cpp} "$out/SFCpuPower.cpp"
    cp ${./src/sf_cpufreq_policy.c} "$out/sf_cpufreq_policy.c"
    cp ${./src/sf_cpufreq_policy.h} "$out/sf_cpufreq_policy.h"
    cp ${../m1-thermal/src/sf_thermal_policy.c} "$out/sf_thermal_policy.c"
    cp ${../m1-thermal/src/sf_thermal_policy.h} "$out/sf_thermal_policy.h"
    cp ${../m1-thermal/kernel/SFSmcSession.cpp} "$out/SFSmcSession.cpp"
    cp ${../m1-thermal/kernel/SFSmcSession.h} "$out/SFSmcSession.h"
  '';
in pd.packages.x86_64-linux.kexts-arm64.overrideAttrs (old: {
  pname = "siliconforest-cpupower";
  src = pkgs.runCommand "siliconforest-cpupower-tree" {} ''
    cp -R ${old.src} "$out"
    chmod -R u+w "$out"
    test ! -e "$out/src/Kernel/Extensions/SFCpuPower"
    cp -R ${copied} "$out/src/Kernel/Extensions/SFCpuPower"
    printf '\nadd_subdirectory(SFCpuPower)\n' >> "$out/src/Kernel/Extensions/CMakeLists.txt"
  '';
  buildPhase = ''
    runHook preBuild
    ninja -C build-nix SFCpuPower.kext
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    bundle=build-nix/src/Kernel/Extensions/SFCpuPower/SFCpuPower.kext
    test -f "$bundle/Contents/MacOS/SFCpuPower"
    mkdir -p "$out/System/Library/Extensions"
    cp -R "$bundle" "$out/System/Library/Extensions/"
    runHook postInstall
  '';
})
