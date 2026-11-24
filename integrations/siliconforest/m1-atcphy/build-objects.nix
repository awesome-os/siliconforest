# SPDX-License-Identifier: MIT
# SDK compilation of the directly ported PHY programs; no attachment or MMIO.
let
  source = import ../rebase/pd.nix;
  inherit (source) pd pkgs;
in pd.packages.x86_64-linux.kexts-arm64.overrideAttrs (old: {
  pname = "siliconforest-atc-objects";
  src = pkgs.runCommand "siliconforest-atc-objects-tree" {} ''
    cp -R ${old.src} "$out"
    chmod -R u+w "$out"
    dest="$out/src/Kernel/Extensions/SFATCLogic"
    mkdir -p "$dest"
    cp ${./include}/* ${./src}/*.c ${./upstream}/*.inc "$dest/"
    cat > "$dest/CMakeLists.txt" <<'CMAKE'
add_library(SFATCLogic OBJECT sf_atc_usb2.c sf_atc_dp.c sf_atc_modes.c sf_atc_calibration.c sf_atc_pipe.c)
target_include_directories(SFATCLogic PRIVATE "''${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(SFATCLogic PRIVATE xnu_kernel_private_headers)
target_compile_options(SFATCLogic PRIVATE -Os -Wall)
CMAKE
    printf '\nadd_subdirectory(SFATCLogic)\n' >> "$out/src/Kernel/Extensions/CMakeLists.txt"
  '';
  buildPhase = ''
    runHook preBuild
    ninja -C build-nix SFATCLogic
    runHook postBuild
  '';
  installPhase = ''
    mkdir -p "$out/objects"
    cp build-nix/src/Kernel/Extensions/SFATCLogic/CMakeFiles/SFATCLogic.dir/*.o "$out/objects/"
    cp ${./NOTICE} "$out/NOTICE"
  '';
})
