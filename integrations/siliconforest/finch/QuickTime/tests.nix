# SPDX-License-Identifier: MIT
let e=import ../environment.nix; a=import ../../cocoa/arm64.nix; headers=import ../media/headers.nix; in
e.pkgs.runCommand "siliconforest-quicktime-private-test-arm64" {} ''
 export DARWIN_SDK_ROOT=${e.sdk};mkdir -p $out/bin
 ${e.cc} -O2 -fblocks -fno-objc-arc -isysroot ${e.sdk} -I${e.libsystem}/usr/include -I${a.base.libobjc}/usr/include -I${a.base.foundation}/usr/include -I${a.base.corefoundation}/include -I${headers} -I${a.appDeps.coregraphics}/usr/include -I${a.appDeps.onyx2d}/usr/include -I${a.src}/src/Frameworks -I${a.mesa}/usr/include -F${a.openglFramework}/System/Library/Frameworks \
  ${./probe.m} --ld-path=${e.ld} -nostdlib -L${e.libsystem}/usr/lib -L${a.base.libobjc}/usr/lib -L${a.base.foundation}/usr/lib -L${a.base.corefoundation}/usr/lib \
  -lFoundation -lCoreFoundation -lobjc -lSystem -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 -o $out/bin/qt-private-probe
''
