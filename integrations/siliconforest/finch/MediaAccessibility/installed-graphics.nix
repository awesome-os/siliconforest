# SPDX-License-Identifier: MIT
# Actual ImageIO metadata provider from the owned Cocoa0109 implementation.
let
  e=import ../environment.nix;
  graphics=import ../../cocoa/patched.nix;
  base=import ./build.nix;
in base.overrideAttrs(old:{
  name="siliconforest-finch-MediaAccessibility-installed-graphics-arm64";
  buildPhase=builtins.replaceStrings
    [ "${e.p.coregraphics-arm64}" "${e.p.onyx2d-arm64}" "${e.p.coretext-arm64}" ]
    [ "${graphics.coregraphics}" "${graphics.onyx2d}" "${graphics.coretext}" ] old.buildPhase;
})
