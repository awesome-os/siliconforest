# SPDX-License-Identifier: MIT
let e=import ../environment.nix;a=import ../../cocoa/arm64.nix;
 trace=import ./cadence-trace.nix;headers=import ../media/headers.nix;
 cv=import ../VideoToolbox/zerocopy/corevideo.nix;
in e.pkgs.runCommand "siliconforest-firefox-media-cadence-probes-arm64" {} ''
 export DARWIN_SDK_ROOT=${e.sdk}
 mkdir -p $out/bin $out/lib
 cp ${trace}/lib/SFMediaCadenceTrace.dylib $out/lib/
 for mode in baseline observed; do
  EXTRA=""; if [ "$mode" = observed ]; then EXTRA="-DSF_HAVE_CADENCE -Wl,-needed_library,${trace}/lib/SFMediaCadenceTrace.dylib -Wl,-rpath,@executable_path/../lib"; fi
  ${e.cc} -O2 -std=c11 -fblocks -D_DARWIN_C_SOURCE -isysroot ${e.sdk} \
   -I${headers} -I${e.libsystem}/usr/include -I${e.p.coreFoundation-arm64}/include -I${a.src}/src/Frameworks -I${a.mesa}/usr/include \
   ${./cadence-probe.c} $EXTRA --ld-path=${e.ld} -nostdlib -L${e.libsystem}/usr/lib -L${e.p.coreFoundation-arm64}/usr/lib \
   -F${cv}/System/Library/Frameworks -F${a.openglFramework}/System/Library/Frameworks -framework CoreVideo -lCoreFoundation -lSystem \
   -Wl,-dylib_file,/usr/lib/libGL.1.dylib:${a.mesa}/usr/lib/libGL.1.dylib \
   -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
   -Wl,-platform_version,macos,26.5,26.5 -o $out/bin/cadence-$mode
 done
''
