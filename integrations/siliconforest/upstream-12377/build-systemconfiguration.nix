# SPDX-License-Identifier: MIT
# Re-evaluate c713's unchanged recipes with the one header in the source filter.
# Replacing only configurePhase text retains Nix's old failing Security context.
let
  inherit (import ../rebase/pd.nix) pd pkgs;
  patched = pkgs.applyPatches {
    name = "siliconforest-selected-trustd-header-source";
    src = pd.outPath;
    patches = [ ./patches/a08eb9b6-trustd-header.patch ];
  };
  fixed = (import (patched + "/flake.nix")).outputs (pd.inputs // { self = fixed; })
    // { outPath = "${patched}"; inherit (pd) inputs; };
in fixed.packages.x86_64-linux.systemConfiguration-arm64
