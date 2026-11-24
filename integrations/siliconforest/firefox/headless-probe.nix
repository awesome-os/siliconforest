# SPDX-License-Identifier: MIT
let
 a = import ../cocoa/arm64.nix;
 inherit (a) pkgs pd;
 sdk = "${pd.apple-sdk}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";
 sys = pd.libsystem-arm64;
 tc = "${pd.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang";
 d = a.appDeps;
in pkgs.runCommand "sffx-headless-appkit-probe-arm64" {} ''
 mkdir -p cf-headers
 ln -s ${a.base.corefoundation}/include cf-headers/CoreFoundation
 ${tc} -O2 -g -fblocks -isysroot ${sdk} -I${sys}/usr/include \
   -I${a.src}/src/Libraries/libsandbox/include \
   -I${a.base.libobjc}/usr/include -I${a.base.foundation}/usr/include -Icf-headers \
   -I${d.appkit}/usr/include -I${d.coregraphics}/usr/include -I${d.coretext}/usr/include -I${d.onyx2d}/usr/include \
   -c ${./headless-probe.m} -o probe.o
 ${tc} -isysroot ${sdk} -fuse-ld=${pd.native-ld}/bin/ld -nostdlib \
   -L${sys}/usr/lib -L${a.base.libobjc}/usr/lib -L${a.base.foundation}/usr/lib \
   -L${a.base.corefoundation}/usr/lib -L${d.appkit}/usr/lib \
   -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${sys}/usr/lib/system/libdyld.dylib \
   -Wl,-platform_version,macos,26.5,26.5 probe.o -lAppKit -lFoundation -lCoreFoundation -lobjc -lSystem -o headless-probe
 mkdir -p $out/bin;cp headless-probe $out/bin/
''
