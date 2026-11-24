# SPDX-License-Identifier: MIT
# Compile the private CI candidate with the corrected dependency's inline APIs.
let
 e=import ../environment.nix;
 base=import ./build.nix;
 geometry=import ./geometry-provider.nix;
in base.overrideAttrs(old:{
 name="siliconforest-finch-CoreImage-finite-geometry-private";
 buildPhase=e.pkgs.lib.replaceStrings
  ["-I${e.p.coregraphics-arm64}/usr/include"]
  ["-I${geometry}/include"] old.buildPhase;
})
