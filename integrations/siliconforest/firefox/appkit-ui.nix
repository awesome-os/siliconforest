# SPDX-License-Identifier: MIT
# Private matched-source UI provider. Never replaces the installed AppKit.
{ appkitSource ? null, menuGeometry ? false, preciseScroll ? false,
  gpuPresentation ? false, traceEvents ? false, pasteboardProvider ? false,
  baseHasUIFixes ? appkitSource == null, baseHasGPUFixes ? appkitSource == null }:
let
 a=import ../cocoa/arm64.nix;
 p=import ../cocoa/patched.nix;
 base=if appkitSource == null then p.appkitSrc else builtins.path {
  path=appkitSource;name="sffx-selected-appkit-source";
 };
 # TERM8 adopted these fixes into the main source. A selected older source
 # can still request them without reapplying or reversing the adopted ones.
 patches=a.pkgs.lib.optional (!baseHasUIFixes) ./cocoa-patches/0212-menu-appearance-and-popup-inheritance.patch
  ++a.pkgs.lib.optional (menuGeometry && !baseHasGPUFixes) ./cocoa-patches/0206-window-class-content-geometry-counts-menu-strip.patch
  ++a.pkgs.lib.optional (preciseScroll && !baseHasUIFixes) ./cocoa-patches/0210-precise-scroll-deltas-and-gesture-phase.patch
  ++a.pkgs.lib.optional (gpuPresentation && !baseHasGPUFixes) ./cocoa-patches/0209-view-registers-gpu-layer-with-private-windowserver.patch
  ++a.pkgs.lib.optional traceEvents ./cocoa-patches/0211-diagnostic-headless-event-loop-state.patch
  ++a.pkgs.lib.optional (pasteboardProvider && !baseHasUIFixes) ./cocoa-patches/0214-pasteboard-item-provider-receives-destination-board.patch;
 src=a.pkgs.runCommand "sffx-appkit-ui-source" {} ''
  mkdir -p $out/src/Frameworks
  cp -r ${base} $out/src/Frameworks/AppKit
  chmod -R u+w $out
  cd $out
  ${a.pkgs.lib.concatMapStringsSep "\n" (patch: ''
   ${a.pkgs.patch}/bin/patch --batch --forward -p1 < ${patch}
  '') patches}
 '';
in (p.appkit.override {src="${src}/src/Frameworks/AppKit";}).overrideAttrs (old:{
 postInstall=(old.postInstall or "")+''
  ln -sfn A $out/System/Library/Frameworks/AppKit.framework/Versions/C
 '';
})
