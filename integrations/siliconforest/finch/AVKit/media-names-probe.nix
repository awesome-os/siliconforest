# SPDX-License-Identifier: MIT
let e=import ../environment.nix; in
e.pkgs.runCommand "siliconforest-media-format-name-probe-arm64" {} ''
 export DARWIN_SDK_ROOT=${e.sdk}
 mkdir -p $out/bin
 ${e.cc} -O2 -isysroot ${e.sdk} -I${e.libsystem}/usr/include -I${e.p.coreFoundation-arm64}/include \
  ${./media-names-probe.c} --ld-path=${e.ld} -nostdlib -L${e.libsystem}/usr/lib \
  -L${e.p.coreFoundation-arm64}/usr/lib -lCoreFoundation -lSystem \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 -o $out/bin/media-names-probe
''
