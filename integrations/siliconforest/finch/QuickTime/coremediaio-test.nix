# SPDX-License-Identifier: MIT
let e=import ../environment.nix; in
e.pkgs.runCommand "siliconforest-coremediaio-unavailable-test-arm64" {} ''
 export DARWIN_SDK_ROOT=${e.sdk};mkdir -p $out/bin
 ${e.cc} -O2 -isysroot ${e.sdk} -I${e.libsystem}/usr/include ${./coremediaio-probe.c} --ld-path=${e.ld} -nostdlib -L${e.libsystem}/usr/lib -lSystem \
 -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
 -Wl,-platform_version,macos,26.5,26.5 -o $out/bin/coremediaio-probe
''
