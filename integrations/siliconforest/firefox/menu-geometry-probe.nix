# SPDX-License-Identifier: MIT
let
 a=import ../cocoa/arm64.nix;
 e=import ../finch/environment.nix;
 app=import ./appkit-menu.nix {};
in e.pkgs.runCommand "sffx-window-menu-geometry-probe-arm64" {} ''
 ${e.cc} -O2 -g -fblocks -isysroot ${e.sdk} -I${e.libsystem}/usr/include \
  -I${a.base.libobjc}/usr/include -I${a.base.foundation}/usr/include -I${a.base.corefoundation}/include \
  -I${app}/usr/include -I${a.appDeps.coregraphics}/usr/include -I${a.appDeps.onyx2d}/usr/include \
  -I${a.src}/src/Frameworks -I${a.mesa}/usr/include -F${a.openglFramework}/System/Library/Frameworks \
  -c ${./menu-geometry-probe.m} -o probe.o
 ${e.cc} probe.o --ld-path=${e.ld} -nostdlib -isysroot ${e.sdk} \
  -L${e.libsystem}/usr/lib -L${a.base.libobjc}/usr/lib -L${a.base.foundation}/usr/lib \
  -L${a.base.corefoundation}/usr/lib -F${app}/System/Library/Frameworks \
  -framework AppKit -lFoundation -lCoreFoundation -lobjc -lSystem \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 -o probe
 mkdir -p $out/bin;cp probe $out/bin/menu-geometry-probe
''
