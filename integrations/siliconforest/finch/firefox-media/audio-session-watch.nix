# SPDX-License-Identifier: MIT
let e=import ../environment.nix;in e.pkgs.runCommand "siliconforest-audio-session-watch-arm64" {} ''
 export DARWIN_SDK_ROOT=${e.sdk}
 mkdir -p $out/bin
 ${e.cc} -std=c11 -O2 -Wall -Wextra -D_DARWIN_C_SOURCE -isysroot ${e.sdk} \
  -I${e.libsystem}/usr/include ${./audio-session-watch.c} --ld-path=${e.ld} -nostdlib \
  -L${e.libsystem}/usr/lib -lSystem \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 -o $out/bin/audio-session-watch
''
