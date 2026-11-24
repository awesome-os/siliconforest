# SPDX-License-Identifier: MIT
# Private follow-up to the owner-adopted 0220 memory-font correction.
let
 a=import ../cocoa/arm64.nix;
 p=import ../cocoa/patched.nix;
 src=a.pkgs.runCommand "sffx-coretext-quality-v2-source" {} ''
  mkdir -p $out/src/Frameworks
  cp -r ${p.coretextSrc} $out/src/Frameworks/CoreText
  chmod -R u+w $out
  cd $out
  ${a.pkgs.patch}/bin/patch --batch --forward -p1 < ${./cocoa-patches/0221-coretext-apply-variation-to-graphics-font.patch}
  ${a.pkgs.patch}/bin/patch --batch --forward -p1 < ${./cocoa-patches/0222-coretext-available-serif-and-generic-aliases.patch}
  ${a.pkgs.patch}/bin/patch --batch --forward -p1 < ${./cocoa-patches/0223-coretext-variation-coordinates-use-numeric-axis-tags.patch}
 '';
in p.coretext.override {src="${src}/src/Frameworks/CoreText";}
