# SPDX-License-Identifier: MIT
let e=import ../environment.nix; in
e.pkgs.runCommand "siliconforest-avkit-capture-item-probe-arm64" {} ''
 export DARWIN_SDK_ROOT=${e.sdk};mkdir -p $out/bin
 ${e.cc} -O2 -fno-objc-arc -fobjc-exceptions -isysroot ${e.sdk} \
  -I${e.libsystem}/usr/include -I${e.p.libobjc-arm64}/usr/include -I${e.p.foundation-arm64}/usr/include -I${e.p.coreFoundation-arm64}/include \
  ${./capture-item-probe.m} --ld-path=${e.ld} -nostdlib -L${e.libsystem}/usr/lib -L${e.p.libobjc-arm64}/usr/lib -L${e.p.foundation-arm64}/usr/lib -lFoundation -lobjc -lSystem \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 -o $out/bin/avkit-capture-item-probe
''
