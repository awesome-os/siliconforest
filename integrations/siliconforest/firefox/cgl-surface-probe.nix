# SPDX-License-Identifier: MIT
let
 a=import ../cocoa/arm64.nix;
 inherit (a) pkgs pd;
 sdk="${pd.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";
 sys=pd.libsystem-arm64;
 tc="${pd.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang";
 surface=import ./surface-gpu.nix;
in pkgs.runCommand "sffx-cgl-iosurface-probe-arm64" {} ''
 mkdir cf-headers
 ln -s ${a.base.corefoundation}/include cf-headers/CoreFoundation
 ${tc} -O2 -g -isysroot ${sdk} -I${sys}/usr/include -Icf-headers -c ${./cgl-surface-probe.c} -o probe.o
 ${tc} -isysroot ${sdk} -fuse-ld=${pd.native-ld}/bin/ld -nostdlib \
  -L${sys}/usr/lib -L${a.base.corefoundation}/usr/lib -L${surface}/Frameworks/IOSurface.framework/Versions/A \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${sys}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 probe.o ${surface}/Frameworks/IOSurface.framework/Versions/A/IOSurface -lCoreFoundation -lSystem -o cgl-surface-probe
 mkdir -p $out/bin;cp cgl-surface-probe $out/bin/
''
