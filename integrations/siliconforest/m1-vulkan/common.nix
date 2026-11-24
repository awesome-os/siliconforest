# SPDX-License-Identifier: MIT
# Inputs of the Vulkan port: those of ../m1-mesa-asahi/common.nix (the SiliconForest flake, its
# nixpkgs, the cross toolchain) with the Mesa source that Omacom's omarchy-pkgs `mesa-aurora`
# packages with -Dvulkan-drivers=asahi: omacom/mesa integration/aurora b7b03c5b3b8 (26.3.0-devel,
# 8 November 2026), built there with no patches.
(import ../m1-mesa-asahi/common.nix) // rec {
  mesaRev = "b7b03c5b3b8abe1e040d6056dc62c01b02d3639c";
  mesaVersion = "26.3.0-devel-b7b03c5b3b8";
  mesaSrc = (import ../m1-mesa-asahi/common.nix).pkgs.fetchurl {
    name = "mesa-b7b03c5b3b8.tar.gz";
    url = "https://github.com/omacom/mesa/archive/${mesaRev}.tar.gz";
    sha256 = "0nyafwm6hv32fsssjil3kwlimlk67nd9af5mwk76q77naxxmlklj";
  };
}
