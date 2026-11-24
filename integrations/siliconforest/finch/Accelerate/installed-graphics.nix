# SPDX-License-Identifier: MIT
# Whole tree against the real ICC/indexed-space provider added by Cocoa0118.
# Keep the bootstrap/default recipes available for the earlier audit receipts.
let
  e=import ../environment.nix;
  graphics=import ../../cocoa/patched.nix;
  base=import ./build.nix {};
in base.overrideAttrs(old:{
  name="siliconforest-finch-Accelerate-whole-installed-graphics-arm64";
  buildPhase=builtins.replaceStrings
    [ "${e.p.coregraphics-arm64}" "${e.p.onyx2d-arm64}" "chmod -R u+w headers" ]
    [ "${graphics.coregraphics}" "${graphics.onyx2d}" ''
        chmod -R u+w headers
        # Real provider declares the ABI-equivalent generic CFTypeRef input;
        # suppress our old missing-header declaration only in this variant.
        sed -i '/^CGColorSpaceRef CGColorSpaceCreateWithICCData(CFDataRef);$/d' headers/Accelerate/Image.h
      '' ] old.buildPhase;
})
