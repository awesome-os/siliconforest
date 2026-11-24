# SPDX-License-Identifier: MIT
{ gpuBacking ? false, nv12Backing ? false }:
assert !nv12Backing || gpuBacking;
let
 a=import ../cocoa/arm64.nix;
 drm=(import ../m1-vulkan/common.nix).p.libdrm-dri-arm64;
 sys=a.pd.libsystem-arm64;
 sdk="${a.pd.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";
 tc="${a.pd.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang";
 patched=a.pkgs.runCommand "sffx-iosurface-private-broker-source" {} ''
  cp -r ${a.src}/src/Frameworks/IOSurface $out;chmod -R u+w $out
  cd $out
  ${a.pkgs.patch}/bin/patch -p1 < ${./iosurface-patches/0001-private-standalone-broker.patch}
  ${a.pkgs.lib.optionalString gpuBacking ''
   ${a.pkgs.patch}/bin/patch -p1 < ${./iosurface-patches/0002-opt-in-dmabuf-fileport-storage.patch}
   ${a.pkgs.lib.optionalString nv12Backing ''
    ${a.pkgs.patch}/bin/patch -p1 < ${./iosurface-patches/0003-nv12-gpu-planes-and-local-use-count.patch}
   ''}
  ''}
  ${a.pkgs.patch}/bin/patch -p1 < ${./iosurface-patches/0004-lookup-never-revives-a-finalizing-surface.patch}
 '';
 raw=a.iosurface.override {src=patched;};
 iso=if gpuBacking then raw.overrideAttrs(old:{
  buildPhase=builtins.replaceStrings ["-Wall" "-lCoreFoundation"]
   ["-Wall -I${drm}/include -I${drm}/include/libdrm" "-L${drm}/lib -ldrm -lCoreFoundation"] old.buildPhase;
 }) else raw;
in a.pkgs.runCommand "sffx-private-iosurface-session-arm64" {} ''
 ${tc} -O2 -g -isysroot ${sdk} -I${sys}/usr/include -c ${./surface-session.c} -o session.o
 ${tc} -isysroot ${sdk} -fuse-ld=${a.pd.native-ld}/bin/ld -nostdlib \
  -L${sys}/usr/lib -lSystem \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${sys}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 session.o -o session
 mkdir -p $out/bin $out/Frameworks
 cp ${iso}/usr/libexec/iosurfaced $out/bin/
 cp -r ${iso}/System/Library/Frameworks/IOSurface.framework $out/Frameworks/
 cp session $out/bin/surface-session
 mkdir cf-headers;ln -s ${a.base.corefoundation}/include cf-headers/CoreFoundation
 ${tc} -O2 -g -isysroot ${sdk} -I${sys}/usr/include -Icf-headers -c ${./surface-probe.c} -o probe.o
 ${tc} -isysroot ${sdk} -fuse-ld=${a.pd.native-ld}/bin/ld -nostdlib \
  -L${sys}/usr/lib -L${a.base.corefoundation}/usr/lib -F${iso}/System/Library/Frameworks \
  -framework IOSurface -lCoreFoundation -lSystem \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${sys}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 probe.o -o $out/bin/surface-probe
''
