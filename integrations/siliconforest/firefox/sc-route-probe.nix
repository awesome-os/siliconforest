# SPDX-License-Identifier: MIT
let
 a=import ../cocoa/arm64.nix;
 inherit(a) pkgs pd;
 sdk="${pd.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";
 system=pd.libsystem-arm64;
 compiler="${pd.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang";
in pkgs.runCommand "sffx-network-notification-probe-arm64" {} ''
 mkdir include;ln -s ${a.base.corefoundation}/include include/CoreFoundation
 cp ${./sc-route-source.h} sc-route-source.h
 ${compiler} -O2 -g -isysroot ${sdk} -I${system}/usr/include -Iinclude -I. \
  -c ${./sc-route-source.c} -o route.o
 ${compiler} -O2 -g -isysroot ${sdk} -I${system}/usr/include -Iinclude -I. \
  -c ${./sc-route-probe.c} -o probe.o
 ${compiler} -isysroot ${sdk} -fuse-ld=${pd.native-ld}/bin/ld -nostdlib \
  -L${system}/usr/lib -L${a.base.corefoundation}/usr/lib \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${system}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 route.o probe.o -lCoreFoundation -lSystem -o sc-route-probe
 mkdir -p $out/bin;cp sc-route-probe $out/bin/
''
