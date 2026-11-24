# SPDX-License-Identifier: MIT
# Actual native IOKit/CoreFoundation/MIG SDK recipe; no hardware access at build.
let
 source=import ../rebase/pd.nix;
 inherit (source) pkgs pd;
 base=pd.packages.x86_64-linux.ioreg-arm64;
in base.overrideAttrs (old: {
 pname="siliconforest-cio-pci-inventory";
 src=pkgs.runCommand "cio-pci-inventory-source" {} ''
  mkdir -p "$out"
  cp ${./tools/sf-cio-pci-inventory.c} "$out/ioreg.c"
  touch "$out/term_stub.c"
 '';
 installPhase=''
  mkdir -p "$out/bin"
  cp ioreg "$out/bin/sf-cio-pci-inventory"
 '';
})
