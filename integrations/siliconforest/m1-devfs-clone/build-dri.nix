# SPDX-License-Identifier: MIT
# Shared SFDRMDri only: frozen installed source, no GPU/DCP driver code changes.
let
  base = import ../m1-dcp/build-kext.nix;
  inherit (import ../rebase/pd.nix) pkgs;
  frozen = builtins.storePath "/nix/store/gj14r67462ndvmf7f2al6ah8isjhk4wc-puredarwin-dcp-tree";
in base.overrideAttrs (old: {
  pname = "siliconforest-dri-devfs-clone";
  src = pkgs.runCommand "siliconforest-dri-devfs-clone-tree" {
    nativeBuildInputs = [ pkgs.python3 pkgs.patch ];
  } ''
    cp -R ${frozen} "$out"
    chmod -R u+w "$out"
    python3 ${./verify-source.py} "$out" ${./manifest.json} dri before
    patch -d "$out" --batch --forward --fuzz=0 -p1 < ${./api.patch}
    patch -d "$out" --batch --forward --fuzz=0 -p1 < ${./dri.patch}
    python3 ${./verify-source.py} "$out" ${./manifest.json} dri after
    python3 ${./verify-source.py} "$out" ${./stats-manifest.json} dri before
    patch -d "$out" --batch --forward --fuzz=0 -p1 < ${./dri-stats.patch}
    python3 ${./verify-source.py} "$out" ${./stats-manifest.json} dri after
  '';
  nativeBuildInputs = (old.nativeBuildInputs or []) ++ [ pkgs.python3 ];
  postConfigure = (old.postConfigure or "") + "\n" + ''
    python3 ${./overlay-header.py} build-nix/src/Kernel/xnu/xnu_header_install src/Kernel/xnu/bsd/miscfs/devfs/devfs.h
  '';
  buildPhase = ''
    runHook preBuild
    ninja -C build-nix SFDRMDri.kext
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    mkdir -p "$out/System/Library/Extensions" "$out/nix-support"
    cp -R build-nix/src/Kernel/Extensions/SFDRMDri/SFDRMDri.kext "$out/System/Library/Extensions/"
    cp ${./manifest.json} "$out/nix-support/source-manifest.json"
    cp devfs-header-overlay.json "$out/nix-support/"
    runHook postInstall
  '';
})
