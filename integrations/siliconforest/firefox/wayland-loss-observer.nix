# SPDX-License-Identifier: MIT
let e=import ../finch/environment.nix;
in e.pkgs.runCommand "sffx-private-wayland-loss-observer-arm64" {} ''
 ${e.cc} -O2 -Wall -Wextra -fPIC -isysroot ${e.sdk} -I${e.libsystem}/usr/include \
  -c ${./wayland-loss-observer.c} -o observer.o
 mkdir -p $out/lib
 ${e.cc} -dynamiclib observer.o --ld-path=${e.ld} -nostdlib -isysroot ${e.sdk} \
  -L${e.libsystem}/usr/lib -lSystem \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 \
  -Wl,-install_name,@rpath/SFFirefoxWaylandLoss.dylib -o $out/lib/SFFirefoxWaylandLoss.dylib
 ${e.cc} -O2 -Wall -Wextra -isysroot ${e.sdk} -I${e.libsystem}/usr/include \
  -c ${./wayland-loss-control.c} -o control.o
 mkdir -p $out/bin
 ${e.cc} control.o --ld-path=${e.ld} -nostdlib -isysroot ${e.sdk} \
  -L${e.libsystem}/usr/lib -lSystem \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 -o $out/bin/firefox
''
