# SPDX-License-Identifier: MIT
let
 a=import ../cocoa/arm64.nix;
 e=import ../finch/environment.nix;
 d=a.appDeps;
in e.pkgs.runCommand "sffx-font-and-view-diagnostics-arm64" {} ''
 ${e.cc} -O2 -g -fblocks -isysroot ${e.sdk} -I${e.libsystem}/usr/include \
  -I${a.base.libobjc}/usr/include -I${a.base.foundation}/usr/include -I${a.base.corefoundation}/include \
  -I${d.appkit}/usr/include -I${d.coregraphics}/usr/include -I${d.coretext}/usr/include -I${d.onyx2d}/usr/include \
  -I${a.src}/src/Frameworks -I${a.mesa}/usr/include -F${a.openglFramework}/System/Library/Frameworks \
  -c ${./font-trace.m} -o trace.o
 ${e.cc} -dynamiclib trace.o --ld-path=${e.ld} -nostdlib -isysroot ${e.sdk} \
  -L${e.libsystem}/usr/lib -L${a.base.libobjc}/usr/lib -L${a.base.foundation}/usr/lib \
  -L${a.base.corefoundation}/usr/lib -L${d.appkit}/usr/lib -L${d.coregraphics}/usr/lib \
  -L${d.coretext}/usr/lib -L${d.onyx2d}/usr/lib \
  -Wl,-install_name,@rpath/libsffx-font-trace.dylib \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 \
  -lAppKit -lCoreText -lCoreGraphics -lOnyx2D -lFoundation -lCoreFoundation -lobjc -lSystem -o trace.dylib
 mkdir -p $out;cp trace.dylib $out/libsffx-font-trace.dylib
''
