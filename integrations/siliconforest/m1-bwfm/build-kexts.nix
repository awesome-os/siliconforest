# Builds IO80211Family.kext (already in the pinned SiliconForest tree, but not in the M1 kernel
# collection) and SFBwfm.kext (injected from this directory) for arm64.
#
# DMA switches (see CMakeLists.txt), default is per-buffer DART mapping:
#   nix-build build-kexts.nix --arg dmaBounce true                       # SF_BWFM_DMA_BOUNCE
#   nix-build build-kexts.nix --arg defines '[ "SF_BWFM_DMA_RX_SLOTS" ]' # or "SF_DART_STRICT"
{ dmaBounce ? false, defines ? [] }:
let
  allDefines = (if dmaBounce then [ "SF_BWFM_DMA_BOUNCE" ] else []) ++ defines;
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
  copied = pkgs.runCommand "siliconforest-bwfm-source" {} ''
    mkdir -p "$out"
    cp ${./CMakeLists.txt} "$out/CMakeLists.txt"
    cp ${./Info.plist} "$out/Info.plist"
    cp ${./OPENBSD_REVISION} "$out/OPENBSD_REVISION"
    cp ${./SFBwfm.cpp} "$out/SFBwfm.cpp"
    cp ${./sf_bwfm_glue.c} "$out/sf_bwfm_glue.c"
    cp ${./sf_bwfm_glue.h} "$out/sf_bwfm_glue.h"
    cp ${./sf_bwfm_compat.h} "$out/sf_bwfm_compat.h"
    cp ${../m1-apcie-host/dart/sf_dart_pool.h} "$out/sf_dart_pool.h"
    cp ${../m1-apcie-host/dart/sf_dart_pool.cpp} "$out/sf_dart_pool.cpp"
    cp ${../m1-apcie-host/dart/sf_dart_map.h} "$out/sf_dart_map.h"
    cp ${../m1-apcie-host/dart/sf_dart_map.cpp} "$out/sf_dart_map.cpp"
    cp ${./bwfm.c} "$out/bwfm.c"
    cp ${./bwfmreg.h} "$out/bwfmreg.h"
    cp ${./bwfmvar.h} "$out/bwfmvar.h"
    cp ${./if_bwfm_pci.c} "$out/if_bwfm_pci.c"
    cp ${./if_bwfm_pci.h} "$out/if_bwfm_pci.h"
    chmod u+w "$out/CMakeLists.txt"
    for d in ${pkgs.lib.concatStringsSep " " allDefines}; do
      printf 'set(%s ON CACHE BOOL "" FORCE)\n' "$d" > "$out/defs.cmake"
      cat "$out/CMakeLists.txt" >> "$out/defs.cmake"
      mv "$out/defs.cmake" "$out/CMakeLists.txt"
    done
  '';
in pd.packages.x86_64-linux.kexts-arm64.overrideAttrs (old: {
  pname = "siliconforest-bwfm";
  src = pkgs.runCommand "siliconforest-bwfm-tree" {} ''
    cp -R ${old.src} "$out"
    chmod -R u+w "$out"
    test -d "$out/src/Kernel/Extensions/IO80211Family"
    test ! -e "$out/src/Kernel/Extensions/SFBwfm"
    # patches/: changes to the tree's IO80211Family (its licence, MPL-2.0 OR BSD-2-Clause)
    for p in ${./patches}/*.patch; do ${pkgs.patch}/bin/patch -d "$out" -p1 --no-backup-if-mismatch < "$p"; done
    cp -R ${copied} "$out/src/Kernel/Extensions/SFBwfm"
    printf '\nadd_subdirectory(SFBwfm)\n' >> "$out/src/Kernel/Extensions/CMakeLists.txt"
  '';
  buildPhase = ''
    runHook preBuild
    ninja -C build-nix IO80211Family.kext SFBwfm.kext
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    mkdir -p "$out/System/Library/Extensions"
    for name in IO80211Family SFBwfm; do
      bundle=build-nix/src/Kernel/Extensions/$name/$name.kext
      test -f "$bundle/Contents/MacOS/$name"
      cp -R "$bundle" "$out/System/Library/Extensions/"
    done
    runHook postInstall
  '';
})
