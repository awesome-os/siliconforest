# SPDX-License-Identifier: MIT
let
  source=import ../rebase/pd.nix;
  inherit (source) pkgs pd;
  base=pd.packages.x86_64-linux.ioreg-arm64;
in base.overrideAttrs (old: {
  pname="siliconforest-cio-prepare-dock-usb";
  src=pkgs.runCommand "cio-prepare-dock-usb-source" {} ''
    mkdir -p "$out"
    cp ${./tools/sf-cio-prepare-dock-usb.c} "$out/ioreg.c"
    touch "$out/term_stub.c"
  '';
  installPhase=''
    mkdir -p "$out/bin"
    cp ioreg "$out/bin/sf-cio-prepare-dock-usb"
  '';
})
