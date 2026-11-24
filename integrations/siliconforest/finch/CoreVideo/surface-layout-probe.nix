# SPDX-License-Identifier: MIT
# Linux-built public-ABI metadata probe; no CoreVideo build/Apple SDK dependency.
let e=import ../environment.nix; in
e.pkgs.runCommand "siliconforest-cv-iosurface-layout-probe-arm64" {} ''
 export DARWIN_SDK_ROOT=${e.sdk}
 mkdir -p $out/bin
 ${e.cc} -O2 -Wall -Wextra -isysroot ${e.sdk} -I${e.libsystem}/usr/include -I${e.p.coreFoundation-arm64}/include \
  ${./surface-layout-probe.c} --ld-path=${e.ld} -nostdlib -L${e.libsystem}/usr/lib \
  -lSystem -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 -o $out/bin/cv-iosurface-layout-probe
''
