# SPDX-License-Identifier: MIT
let e=import ../environment.nix;cm=import ../CoreMedia/build.nix;
in e.pkgs.runCommand "siliconforest-qt-vt-capability-probe-arm64" {} ''
 export DARWIN_SDK_ROOT=${e.sdk}
 mkdir -p $out/bin
 ${e.cc} -O2 -isysroot ${e.sdk} -I${e.libsystem}/usr/include -I${e.p.coreFoundation-arm64}/include -I${../CoreMedia/include} \
  ${./vt-capability-probe.c} --ld-path=${e.ld} -nostdlib -L${e.libsystem}/usr/lib -L${e.p.coreFoundation-arm64}/usr/lib \
  -F${cm}/System/Library/Frameworks -framework CoreMedia -lCoreFoundation -lSystem \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 -o $out/bin/vt-capability-probe
''
