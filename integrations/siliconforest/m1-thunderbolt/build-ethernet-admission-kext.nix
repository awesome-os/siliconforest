# SPDX-License-Identifier: MIT
# Dedicated real I210 startup guard and checked late bus mastering.
let
 base=import ./build-ethernet-kext.nix;
 inherit (import ../m1-rust-kext/pins.nix) pkgs;
in base.overrideAttrs (old: {
 pname="siliconforest-t8103-dock-ethernet-admission";
 src=pkgs.runCommand "siliconforest-dock-ethernet-admission-tree" {nativeBuildInputs=[pkgs.python3];} ''
  cp -R ${old.src} "$out"
  chmod -R u+w "$out"
  python3 ${./tools/prepare_ethernet_admission.py} "$out"
  python3 ${./tools/prepare_ethernet_coherent_dma.py} "$out"
 '';
})
