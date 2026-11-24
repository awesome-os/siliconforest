# SPDX-License-Identifier: MIT
let
  source=import ../rebase/pd.nix;
  inherit (source) pkgs pd;
  base=pd.packages.x86_64-linux.ioreg-arm64;
in base.overrideAttrs (old: {
  pname="siliconforest-cio-retrain-peerless";
  src=pkgs.runCommand "cio-retrain-peerless-source" {} ''
    mkdir -p "$out"
    cp ${./tools/sf-cio-retrain-peerless.c} "$out/ioreg.c"
    touch "$out/term_stub.c"
  '';
  installPhase=''
    mkdir -p "$out/bin"
    cp ioreg "$out/bin/sf-cio-retrain-peerless"
  '';
})
