# SPDX-License-Identifier: MIT
# Private backing-pixel render target; wrapper identity follows terminal-app.
{ skipUnchangedTransactions ? false, detachInvalidatedLayers ? true, gpuPresentation ? false,
  releaseContextResources ? false, coalesceNotifications ? false, baseSource ? null,
  baseHasGPUFixes ? baseSource == null }:
let
 a = import ../cocoa/arm64.nix;
 p = import ../cocoa/patched.nix;
 src = a.pkgs.runCommand "sffx-quartz-backing-scale-source" {} ''
  mkdir -p $out/src/Frameworks
  cp -r ${if baseSource == null then p.quartzcoreSrc else baseSource} $out/src/Frameworks/QuartzCore
  chmod -R u+w $out
  cd $out
  ${a.pkgs.lib.optionalString (!detachInvalidatedLayers) ''
    ${a.pkgs.patch}/bin/patch -R -p1 < ${./cocoa-patches/0207-layer-context-detaches-retained-tree.patch}
  ''}
  # TERM9's main source already has 0208/0213/0216/0219q. Only explicitly
  # selected older snapshots need the private exporting series again.
  ${a.pkgs.lib.optionalString (gpuPresentation && !baseHasGPUFixes) ''
    ${a.pkgs.patch}/bin/patch -p1 < ${./cocoa-patches/0208-layer-context-exportable-gpu-frame.patch}
    ${a.pkgs.patch}/bin/patch -p1 < ${./cocoa-patches/0213-cache-gpu-frame-export-provider-lookups.patch}
    ${a.pkgs.patch}/bin/patch -p1 < ${./cocoa-patches/0216-opt-in-gpu-frame-reuse-until-tree-mutation.patch}
    ${a.pkgs.lib.optionalString coalesceNotifications ''
      ${a.pkgs.patch}/bin/patch -p1 < ${./cocoa-patches/0218-opt-in-gpu-notification-refresh-coalescing.patch}
    ''}
    ${a.pkgs.lib.optionalString releaseContextResources ''
      ${a.pkgs.patch}/bin/patch -p1 < ${./cocoa-patches/0219-layer-context-releases-owned-renderer-and-cgl-resources.patch}
    ''}
  ''}
  ${a.pkgs.lib.optionalString (gpuPresentation && baseHasGPUFixes && coalesceNotifications) ''
    ${a.pkgs.patch}/bin/patch -p1 < ${./cocoa-patches/0218-opt-in-gpu-notification-refresh-coalescing.patch}
  ''}
  ${a.pkgs.lib.optionalString skipUnchangedTransactions ''
    ${a.pkgs.patch}/bin/patch -p1 < ${./cocoa-patches/0205-layer-transactions-skip-unchanged-contents.patch}
  ''}
 '';

 q = a.pd.quartzcore-msm-arm64.override { src = "${src}/src/Frameworks/QuartzCore"; };
 qp = q.overrideAttrs (old: { buildPhase = builtins.replaceStrings [ "/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore" ] [ "/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore.pd" ] old.buildPhase; });
 sys = a.pd.libsystem-arm64;
 sdk = "${a.pd.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";
 tc = "${a.pd.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang";
in a.pkgs.runCommand "sffx-quartz-backing-scale-wrapper" {} ''
 F=$out/Frameworks/QuartzCore.framework
 mkdir -p $F/Versions/A/Resources
 cp ${qp}/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore $F/Versions/A/QuartzCore.pd
 cp ${qp}/System/Library/Frameworks/QuartzCore.framework/Versions/A/Resources/Info.plist $F/Versions/A/Resources/
 ${tc} -fblocks -isysroot ${sdk} -I${sys}/usr/include \
   -I${a.base.libobjc}/usr/include -I${a.base.foundation}/usr/include \
   -c ${../terminal-app/compat/QuartzCore.m} -o wrapper.o
 ${tc} -dynamiclib -isysroot ${sdk} -fuse-ld=${a.pd.native-ld}/bin/ld -nostdlib \
   -L${sys}/usr/lib -L${a.base.libobjc}/usr/lib -L${a.base.foundation}/usr/lib \
   -Wl,-reexport_library,$F/Versions/A/QuartzCore.pd \
   -Wl,-install_name,/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore.sfcompat \
   -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${sys}/usr/lib/system/libdyld.dylib \
   -Wl,-platform_version,macos,26.5,26.5 wrapper.o -lFoundation -lobjc -lSystem -o $F/Versions/A/QuartzCore
 ln -s A $F/Versions/Current;ln -s Versions/Current/QuartzCore $F/QuartzCore;ln -s Versions/Current/Resources $F/Resources
''
