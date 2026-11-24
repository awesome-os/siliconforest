# SPDX-License-Identifier: MIT
# SFDcp exactly as installed in CIO29 (derivation differs from today's recipe ONLY in src,
# see verify-drv.py), with only killable.patch applied to that frozen tree.
{ patched ? true }:
let
  base = import ../m1-dcp/build-kext.nix;
  inherit (import ../rebase/pd.nix) pkgs;
  frozen = builtins.storePath "/nix/store/gj14r67462ndvmf7f2al6ah8isjhk4wc-puredarwin-dcp-tree";
  tree = pkgs.runCommand "siliconforest-dcp-killable-tree" { nativeBuildInputs = [ pkgs.patch ]; } ''
    cp -R ${frozen} "$out"
    chmod -R u+w "$out"
    patch -d "$out" --batch --forward --fuzz=0 -p1 < ${./killable.patch}
  '';
in base.overrideAttrs (_: if patched then { pname = "siliconforest-dcp-killable"; src = tree; } else { src = frozen; })
