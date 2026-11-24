# SPDX-License-Identifier: MIT
let e=import ../environment.nix; cross=import ../Network/toolchain.nix; runtime=import ../../terminal-app/build-swift-runtime.nix; providers=import ./build.nix; in
e.pkgs.runCommand "siliconforest-quicktime-private-swift-test-arm64" {} ''
 mkdir -p $out/bin
 F=${providers}/System/Library/PrivateFrameworks/FeatureFlags.framework/Versions/A
 ${cross}/swiftc -module-cache-path $TMPDIR/modules -I$F/Modules -emit-object -o probe.o ${./feature-probe.swift}
 ${e.cc} probe.o --ld-path=${e.ld} -nostdlib -isysroot ${e.sdk} -L${runtime}/usr/lib/swift -L${e.libsystem}/usr/lib  $F/FeatureFlags -lswiftCore -lSystem \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 -Wl,-rpath,/usr/lib/swift -o $out/bin/qt-private-swift-probe
''
