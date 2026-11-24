# SPDX-License-Identifier: MIT
let
 e=import ../environment.nix;
 inherit(e) pkgs p cc ld sdk libsystem;
 metal=import ./build.nix;
in pkgs.runCommand "siliconforest-metal-policy-probe-arm64" {} ''
 export DARWIN_SDK_ROOT=${sdk}
 mkdir -p $out/bin
 ${cc} -isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fblocks -fobjc-exceptions -I${libsystem}/usr/include -I${p.foundation-arm64}/usr/include -I${p.coreFoundation-arm64}/include -I${p.libobjc-arm64}/usr/include ${./probe.m} --ld-path=${ld} -nostdlib -L${libsystem}/usr/lib -L${p.foundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib -L${p.coreFoundation-arm64}/usr/lib -F${metal}/System/Library/Frameworks -framework Metal -lFoundation -lCoreFoundation -lobjc -lSystem -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -o $out/bin/metal-policy-probe
''
