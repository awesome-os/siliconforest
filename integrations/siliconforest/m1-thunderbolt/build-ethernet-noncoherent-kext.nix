# SPDX-License-Identifier: MIT
# Detached PCIe-C normal-noncacheable DMA variant, outside every boot profile.
let
 base=import ./build-ethernet-trace-kext.nix;
 inherit (import ../m1-rust-kext/pins.nix) pkgs;
in base.overrideAttrs (old: {
 pname="siliconforest-t8103-dock-ethernet-noncoherent";
 src=pkgs.runCommand "siliconforest-dock-ethernet-noncoherent-tree" {nativeBuildInputs=[pkgs.python3];} ''
  cp -R ${old.src} "$out"
  chmod -R u+w "$out"
  python3 ${./tools/prepare_ethernet_noncoherent.py} "$out"
 '';
})
