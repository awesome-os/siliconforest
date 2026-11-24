# SPDX-License-Identifier: MIT
# FXLEAD: the Firefox runtime's IOSurface (private broker + dma-buf storage, as surface-gpu.nix builds it)
# with and without the lookup try-retain fix, and a CPU-only probe that races a surface's last release
# against lookups of its Mach port. Select a provider with DYLD_FRAMEWORK_PATH=$out/{old,new}/Frameworks.
let
 a=import ../../cocoa/arm64.nix;
 drm=(import ../../m1-vulkan/common.nix).p.libdrm-dri-arm64;
 sys=a.pd.libsystem-arm64;
 sdk="${a.pd.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";
 tc="${a.pd.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang";
 source=fix: a.pkgs.runCommand "fxlead-iosurface-source${if fix then "-tryretain" else ""}" {} ''
  cp -r ${a.src}/src/Frameworks/IOSurface $out;chmod -R u+w $out
  cd $out
  ${a.pkgs.patch}/bin/patch -p1 < ${../iosurface-patches/0001-private-standalone-broker.patch}
  ${a.pkgs.patch}/bin/patch -p1 < ${../iosurface-patches/0002-opt-in-dmabuf-fileport-storage.patch}
  ${a.pkgs.lib.optionalString fix "${a.pkgs.patch}/bin/patch -p1 < ${./iosurface-0004-lookup-never-revives-a-finalizing-surface.patch}"}
 '';
 build=fix: (a.iosurface.override {src=source fix;}).overrideAttrs(old:{
  buildPhase=builtins.replaceStrings ["-Wall" "-lCoreFoundation"]
   ["-Wall -I${drm}/include -I${drm}/include/libdrm" "-L${drm}/lib -ldrm -lCoreFoundation"] old.buildPhase;
 });
 old=build false; new=build true;
in a.pkgs.runCommand "fxlead-surface-lookup-race" {} ''
 mkdir -p $out/bin $out/old/Frameworks $out/new/Frameworks cf-headers
 cp -r ${old}/System/Library/Frameworks/IOSurface.framework $out/old/Frameworks/
 cp -r ${new}/System/Library/Frameworks/IOSurface.framework $out/new/Frameworks/
 ln -s ${a.base.corefoundation}/include cf-headers/CoreFoundation
 ${tc} -O2 -g -isysroot ${sdk} -I${sys}/usr/include -Icf-headers -c ${./surface-lookup-race-probe.c} -o probe.o
 ${tc} -isysroot ${sdk} -fuse-ld=${a.pd.native-ld}/bin/ld -nostdlib \
  -L${sys}/usr/lib -L${a.base.corefoundation}/usr/lib -F${old}/System/Library/Frameworks \
  -framework IOSurface -lCoreFoundation -lSystem \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${sys}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 probe.o -o $out/bin/surface-lookup-race-probe
''
