# SPDX-License-Identifier: MIT
let e=import ../environment.nix; a=import ../../cocoa/arm64.nix; cm=import ../CoreMedia/build.nix; headers=import ../media/headers.nix; in
e.pkgs.runCommand "siliconforest-media-math-reference-arm64" {} ''
 export DARWIN_SDK_ROOT=${e.sdk}
 mkdir -p $out/bin
 ${e.cc} -O2 -isysroot ${e.sdk} -I${e.libsystem}/usr/include -I${headers} \
  -I${e.p.coreFoundation-arm64}/include -I${a.appDeps.coregraphics}/usr/include -I${a.appDeps.onyx2d}/usr/include -I${a.src}/src/Frameworks -I${a.mesa}/usr/include -F${a.openglFramework}/System/Library/Frameworks ${./math-reference.c} \
  --ld-path=${e.ld} -nostdlib -L${e.libsystem}/usr/lib -L${e.p.coreFoundation-arm64}/usr/lib \
  -F${cm}/System/Library/Frameworks -framework CoreMedia -lCoreFoundation -lSystem \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 -o $out/bin/media-math-reference
''
