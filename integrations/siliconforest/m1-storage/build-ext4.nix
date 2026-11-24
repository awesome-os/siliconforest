# SPDX-License-Identifier: MIT
# The tree's ext4.kext with ./ext4-downgrade.patch (a tree patch against the tip): a mounted
# filesystem downgraded to read-only (mount -u -o rdonly) is left on the device exactly as a
# clean unmount leaves it and is not written again. Used by build-kc-rw.nix (the write-fenced
# collection) and build-qemu-image.nix (the rehearsal); no default collection is changed.
let
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
in pd.packages.x86_64-linux.kexts-arm64.overrideAttrs (old: {
  pname = "siliconforest-ext4-downgrade";
  src = pkgs.applyPatches { name = "siliconforest-ext4-downgrade-tree"; src = old.src; patches = [ ./ext4-downgrade.patch ]; };
  buildPhase = ''
    runHook preBuild
    ninja -j6 -C build-nix ext4.kext
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    bundle=build-nix/src/Kernel/Extensions/ext4/ext4.kext
    test -f "$bundle/Contents/MacOS/ext4"
    mkdir -p "$out/System/Library/Extensions"
    cp -R "$bundle" "$out/System/Library/Extensions/"
    runHook postInstall
  '';
})
