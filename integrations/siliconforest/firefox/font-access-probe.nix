# SPDX-License-Identifier: MIT
let
 a=import ../cocoa/arm64.nix;
 e=import ../finch/environment.nix;
in a.pkgs.runCommand "sffx-private-font-access-probe-arm64" {} ''
 ${e.cc} -O2 -isysroot ${e.sdk} -I${e.libsystem}/usr/include \
  -c ${./font-access-probe.c} -o probe.o
 ${e.cc} probe.o --ld-path=${e.ld} -nostdlib -isysroot ${e.sdk} \
  -L${e.libsystem}/usr/lib \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 -lSystem -o probe
 mkdir -p $out/bin;cp probe $out/bin/font-access-probe
''
