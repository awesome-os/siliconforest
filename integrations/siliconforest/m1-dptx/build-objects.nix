# SPDX-License-Identifier: MIT
let
  source = import ../rebase/pd.nix;
  inherit (source) pd pkgs;
in pd.packages.x86_64-linux.kexts-arm64.overrideAttrs (old: {
  pname = "siliconforest-dcpext-plan-objects";
  src = pkgs.runCommand "siliconforest-dcpext-plan-tree" {} ''
    cp -R ${old.src} "$out"
    chmod -R u+w "$out"
    dest="$out/src/Kernel/Extensions/SFDcpExtPlan"
    mkdir -p "$dest"
    cp ${./include}/* ${./src}/*.c ${./upstream}/*.inc "$dest/"
    cat > "$dest/CMakeLists.txt" <<'CMAKE'
add_library(SFDcpExtPlan OBJECT sf_dcpext_plan.c sf_dpxbar.c)
target_include_directories(SFDcpExtPlan PRIVATE "''${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(SFDcpExtPlan PRIVATE xnu_kernel_private_headers)
target_compile_options(SFDcpExtPlan PRIVATE -Os -Wall)
CMAKE
    printf '\nadd_subdirectory(SFDcpExtPlan)\n' >> "$out/src/Kernel/Extensions/CMakeLists.txt"
  '';
  buildPhase = ''
    runHook preBuild
    ninja -C build-nix SFDcpExtPlan
    runHook postBuild
  '';
  installPhase = ''
    mkdir -p "$out/objects"
    cp build-nix/src/Kernel/Extensions/SFDcpExtPlan/CMakeFiles/SFDcpExtPlan.dir/*.o "$out/objects/"
    cp ${./NOTICE} "$out/NOTICE"
  '';
})
