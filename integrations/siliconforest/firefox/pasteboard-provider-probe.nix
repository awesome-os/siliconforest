# SPDX-License-Identifier: MIT
let
 a=import ../cocoa/arm64.nix;
 e=import ../finch/environment.nix;
 d=a.appDeps;
in a.pkgs.runCommand "sffx-pasteboard-provider-probe-arm64" {} ''
 ${e.cc} -O2 -g -fblocks -isysroot ${e.sdk} -I${e.libsystem}/usr/include \
  -I${a.base.libobjc}/usr/include -I${a.base.foundation}/usr/include -I${a.base.corefoundation}/include \
  -I${d.appkit}/usr/include -I${a.src}/src/Frameworks -I${a.mesa}/usr/include \
  -F${a.openglFramework}/System/Library/Frameworks -c ${./pasteboard-provider-probe.m} -o probe.o
 ${e.cc} probe.o --ld-path=${e.ld} -nostdlib -isysroot ${e.sdk} \
  -L${e.libsystem}/usr/lib -L${a.base.libobjc}/usr/lib -L${a.base.foundation}/usr/lib \
  -L${a.base.corefoundation}/usr/lib -L${d.appkit}/usr/lib \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 -lAppKit -lFoundation -lCoreFoundation -lobjc -lSystem -o probe
 mkdir -p $out/bin;cp probe $out/bin/pasteboard-provider-probe
''
