# SPDX-License-Identifier: MIT
# Private paired scrolling provider; never an installed AppKit replacement.
{ appkitSource ? null }:
let
 a=import ../cocoa/arm64.nix;
 p=import ../cocoa/patched.nix;
 base=if appkitSource == null then p.appkitSrc else builtins.path {path=appkitSource;name="sffx-selected-appkit-source";};
 src=a.pkgs.runCommand "sffx-appkit-precise-scroll-source" {} ''
  mkdir -p $out/src/Frameworks
  cp -r ${base} $out/src/Frameworks/AppKit
  chmod -R u+w $out
  cd $out
  ${a.pkgs.patch}/bin/patch --batch -p1 < ${./cocoa-patches/0210-precise-scroll-deltas-and-gesture-phase.patch}
 '';
in (p.appkit.override {src="${src}/src/Frameworks/AppKit";}).overrideAttrs (old: {
 postInstall = (old.postInstall or "") + ''
  ln -sfn A $out/System/Library/Frameworks/AppKit.framework/Versions/C
 '';
})
