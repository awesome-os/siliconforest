# Applies 0001-m1-pcie-wifi-kexts.patch to the pinned SiliconForest tree and builds the three kexts from
# their in-tree locations. This is the check that the patch applies and builds as the tree would.
let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
in pd.packages.x86_64-linux.kexts-arm64.overrideAttrs (old: {
  pname = "siliconforest-m1-pcie-wifi-from-patch";
  src = pkgs.runCommand "siliconforest-m1-pcie-wifi-tree" { nativeBuildInputs = [ pkgs.patch ]; } ''
    cp -R ${old.src} "$out"
    chmod -R u+w "$out"
    cd "$out"
    patch -p1 < ${./0001-m1-pcie-wifi-kexts.patch}
    test -f src/Kernel/Extensions/SFApcieT8103Bridge/SFApcieT8103Link.cpp
    test -f src/Kernel/Extensions/SFBwfm/if_bwfm_pci.c
  '';
  buildPhase = ''
    runHook preBuild
    ninja -C build-nix SFApcieT8103Bridge.kext IO80211Family.kext SFBwfm.kext
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    mkdir -p "$out/System/Library/Extensions"
    for name in SFApcieT8103Bridge IO80211Family SFBwfm; do
      bundle=build-nix/src/Kernel/Extensions/$name/$name.kext
      test -f "$bundle/Contents/MacOS/$name"
      cp -R "$bundle" "$out/System/Library/Extensions/"
    done
    runHook postInstall
  '';
})
