# SPDX-License-Identifier: MIT
# SFAgx exactly as installed in CIO29 (its derivation differs from today's recipe ONLY in
# src, see verify-drv.py), with only killable.patch applied to that frozen tree. The
# Rust object (rust/asahi.o in the tree) is unchanged; its signal_pending() helper links
# to the patched layer function.
{ patched ? true }:
let
  base = import ../m1-rust-kext/build-agx-kext.nix;
  inherit (import ../rebase/pd.nix) pkgs;
  frozen = builtins.storePath "/nix/store/0n94n35s2kylg2c787f94p4mm417mqhs-puredarwin-agx-tree";
  tree = pkgs.runCommand "siliconforest-agx-killable-tree" { nativeBuildInputs = [ pkgs.patch ]; } ''
    cp -R ${frozen} "$out"
    chmod -R u+w "$out"
    patch -d "$out" --batch --forward --fuzz=0 -p1 < ${./killable.patch}
  '';
in base.overrideAttrs (_: if patched then { pname = "siliconforest-agx-killable"; src = tree; } else { src = frozen; })
