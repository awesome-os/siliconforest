# SPDX-License-Identifier: MIT
let
 a=import ../cocoa/arm64.nix;
 sys=a.pd.libsystem-arm64;
 sdk="${a.pd.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";
 cc="${a.pd.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang";
in a.pkgs.runCommand "sffx-gl-timer-probe-arm64" {} ''
 ${cc} -O2 -g -isysroot ${sdk} -I${sys}/usr/include -c ${./gl-timer-probe.c} -o probe.o
 ${cc} -isysroot ${sdk} -fuse-ld=${a.pd.native-ld}/bin/ld -nostdlib probe.o \
  -L${sys}/usr/lib -lSystem \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${sys}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 -o probe
 mkdir -p $out/bin;cp probe $out/bin/gl-timer-probe
''
