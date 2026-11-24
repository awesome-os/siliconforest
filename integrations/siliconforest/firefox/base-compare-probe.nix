# SPDX-License-Identifier: MIT
let
 a=import ../cocoa/arm64.nix;
 e=import ../finch/environment.nix;
in e.pkgs.runCommand "sffx-base-compare-probe-arm64" {} ''
 ${e.cc} -O2 -g -isysroot ${e.sdk} -I${e.libsystem}/usr/include \
  -I${a.base.corefoundation}/include -I${./.} -c ${./base-compare-probe.c} -o probe.o
 ${e.cc} probe.o --ld-path=${e.ld} -nostdlib -isysroot ${e.sdk} \
  -L${e.libsystem}/usr/lib -L${a.base.corefoundation}/usr/lib -lCoreFoundation -lSystem \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 -o probe
 mkdir -p $out/bin;cp probe $out/bin/base-compare-probe
''
