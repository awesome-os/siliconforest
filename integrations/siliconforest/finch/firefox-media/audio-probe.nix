# SPDX-License-Identifier: MIT
let e=import ../environment.nix;unit=import ../AudioToolbox/units/build.nix;observer=import ./hal-observer.nix; in
e.pkgs.runCommand "siliconforest-firefox-media-audio-probe-arm64" {} ''
 export DARWIN_SDK_ROOT=${e.sdk}
 mkdir -p $out/bin $out/lib
 cp ${observer}/lib/SFFirefoxMediaObserver.dylib $out/lib/
 ${e.cc} -O2 -std=c11 -D_DARWIN_C_SOURCE -isysroot ${e.sdk} \
  -I${unit}/usr/include -I${e.libsystem}/usr/include -I${e.p.coreFoundation-arm64}/include \
  ${./audio-probe.c} --ld-path=${e.ld} -nostdlib -L${e.libsystem}/usr/lib \
  -F${unit}/System/Library/Frameworks -framework AudioUnit -framework CoreAudio -lSystem \
  -Wl,-needed_library,${observer}/lib/SFFirefoxMediaObserver.dylib \
  -Wl,-rpath,@executable_path/../lib \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 -o $out/bin/audio-media-probe
''
