# SPDX-License-Identifier: MIT
let
 source=import ../rebase/pd.nix;
 inherit (source) pkgs pd;
 base=pd.packages.x86_64-linux.ioreg-arm64;
in base.overrideAttrs (old: {
 pname="siliconforest-register-dock-interface";
 src=pkgs.runCommand "register-dock-interface-source" {} ''
  mkdir -p "$out"
  cp ${./tools/sf-dock-register-interface.c} "$out/ioreg.c"
  touch "$out/term_stub.c"
 '';
 installPhase=''
  mkdir -p "$out/bin"
  cp ioreg "$out/bin/sf-dock-register-interface"
 '';
})
