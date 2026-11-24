# SPDX-License-Identifier: MIT
let
 e=import ../../environment.nix;
 unit=import ./build.nix;
 inherit(e) pkgs cc ld sdk libsystem;
 fw="${unit}/System/Library/Frameworks";
in pkgs.runCommand "siliconforest-auhal-device-probe-arm64" {} ''
 export DARWIN_SDK_ROOT=${sdk}
 mkdir -p $out/bin
 ${cc} -isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -I${unit}/usr/include -I${libsystem}/usr/include -I${e.p.coreFoundation-arm64}/include ${./probe.c} --ld-path=${ld} -nostdlib -L${libsystem}/usr/lib -L${e.p.coreFoundation-arm64}/usr/lib -F${fw} -framework AudioUnit -framework CoreAudio -lCoreFoundation -lSystem -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -o $out/bin/auhal-device-probe
 ''
