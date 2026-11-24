# SPDX-License-Identifier: MIT
let
 a=import ../cocoa/arm64.nix;
 inherit (a) pkgs pd;
 sdk="${pd.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";
 sys=pd.libsystem-arm64;
 tc="${pd.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang";
in pkgs.runCommand "sffx-frame-id-export-probe-arm64" {} ''
 mkdir cf-headers
 ln -s ${a.base.corefoundation}/include cf-headers/CoreFoundation
 ${tc} -O2 -g -isysroot ${sdk} -I${sys}/usr/include -Icf-headers -c ${./frame-id-export-probe.c} -o probe.o
 ${tc} -isysroot ${sdk} -fuse-ld=${pd.native-ld}/bin/ld -nostdlib \
  -L${sys}/usr/lib -L${a.base.corefoundation}/usr/lib \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${sys}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 probe.o -lCoreFoundation -lSystem -o probe
 mkdir -p $out/bin;cp probe $out/bin/frame-id-export-probe
''
