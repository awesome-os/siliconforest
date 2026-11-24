# SPDX-License-Identifier: MIT
let e=import ../environment.nix;a=import ../../cocoa/arm64.nix;
 headers=import ../media/headers.nix;vt=import ../VideoToolbox/zerocopy/build.nix;
 cv=import ../VideoToolbox/zerocopy/corevideo.nix;cm=import ../CoreMedia/build.nix;
in e.pkgs.runCommand "siliconforest-firefox-media-cadence-trace-arm64" {} ''
 export DARWIN_SDK_ROOT=${e.sdk}
 mkdir -p $out/lib
 ${e.cc} -O2 -std=c11 -fblocks -D_DARWIN_C_SOURCE -fPIC -dynamiclib -isysroot ${e.sdk} \
  -I${headers} -I${../../m1-avd/videotoolbox} -I${e.libsystem}/usr/include -I${e.p.coreFoundation-arm64}/include \
  -I${a.src}/src/Frameworks -I${a.mesa}/usr/include \
  ${./cadence-trace.c} --ld-path=${e.ld} -nostdlib -L${e.libsystem}/usr/lib -L${e.p.coreFoundation-arm64}/usr/lib \
  -F${vt}/System/Library/Frameworks -F${cv}/System/Library/Frameworks -F${cm}/System/Library/Frameworks \
  -F${a.openglFramework}/System/Library/Frameworks \
  -framework VideoToolbox -framework CoreVideo -framework CoreMedia -framework OpenGL -L${a.mesa}/usr/lib -L${a.glu}/usr/lib -lCoreFoundation -lSystem \
  -Wl,-dylib_file,/usr/lib/libGL.1.dylib:${a.mesa}/usr/lib/libGL.1.dylib \
  -Wl,-dylib_file,/usr/lib/libGLU.1.dylib:${a.glu}/usr/lib/libGLU.1.dylib \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 -install_name @rpath/SFMediaCadenceTrace.dylib -o $out/lib/SFMediaCadenceTrace.dylib
''
