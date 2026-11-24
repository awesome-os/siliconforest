# SPDX-License-Identifier: MIT
# Experimental private GPU registration; default follows the Cocoa owner.
# A caller may select an already assembled source for matched A/B controls.
{ appkitSource ? null, traceEvents ? false }:
let
 a=import ../cocoa/arm64.nix;
 p=import ../cocoa/patched.nix;
 base=if appkitSource == null then p.appkitSrc else builtins.path {path=appkitSource;name="sffx-selected-appkit-source";};
 src=a.pkgs.runCommand "sffx-appkit-gpu-source" {} ''
  mkdir -p $out/src/Frameworks
  cp -r ${base} $out/src/Frameworks/AppKit
  chmod -R u+w $out
  cd $out
  ${a.pkgs.patch}/bin/patch --batch -p1 < ${./cocoa-patches/0209-view-registers-gpu-layer-with-private-windowserver.patch}
  ${a.pkgs.lib.optionalString traceEvents ''
   ${a.pkgs.patch}/bin/patch --batch -p1 < ${./cocoa-patches/0211-diagnostic-headless-event-loop-state.patch}
  ''}
 '';
in (p.appkit.override {src="${src}/src/Frameworks/AppKit";}).overrideAttrs (old: {
 postInstall = (old.postInstall or "") + ''
  ln -sfn A $out/System/Library/Frameworks/AppKit.framework/Versions/C
 '';
})
