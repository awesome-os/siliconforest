# SPDX-License-Identifier: MIT
# M1-only actual GPU-output test. No SDK headers or native builds.
let e=import ../../environment.nix; a=import ../../../cocoa/arm64.nix;
 ff=import ./ffmpeg.nix; user=import ../../../m1-avd/build-user.nix;
 vt=import ./build.nix;cm=import ../../CoreMedia/build.nix;cv=import ./corevideo.nix;
 headers=import ../../media/headers.nix;
in e.pkgs.runCommand "siliconforest-vt-external-capture-probe-arm64" {} ''
 export DARWIN_SDK_ROOT=${e.sdk}
 mkdir -p $out/bin
 cp ${../../../m1-avd/videotoolbox/vt-decode-test.c} capture-probe.c
 cp ${./final-gpu-picture.h} final-gpu-picture.h
 chmod u+w capture-probe.c
 ${e.pkgs.python3}/bin/python3 ${./capture-probe.py} capture-probe.c
 ${e.cc} -O2 -isysroot ${e.sdk} -fno-stack-protector -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -D_DARWIN_C_SOURCE \
  -I${headers} -I${../../../m1-avd/videotoolbox} -I${../../../m1-avd/user} -I${ff}/usr/include \
  -I${e.libsystem}/usr/include -I${a.base.corefoundation}/include -I${a.src}/src/Frameworks -I${a.mesa}/usr/include -F${a.openglFramework}/System/Library/Frameworks \
  capture-probe.c --ld-path=${e.ld} -nostdlib -L${e.libsystem}/usr/lib -L${a.base.corefoundation}/usr/lib \
  ${ff}/usr/lib/libavformat.a ${ff}/usr/lib/libavcodec.a ${ff}/usr/lib/libswresample.a ${ff}/usr/lib/libavutil.a ${user}/lib/libsfavd.a \
  -F${vt}/System/Library/Frameworks -F${cm}/System/Library/Frameworks -F${cv}/System/Library/Frameworks -F${a.iosurface}/System/Library/Frameworks \
  -framework VideoToolbox -framework CoreMedia -framework CoreVideo -framework IOSurface -lCoreFoundation -lSystem \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 -o $out/bin/capture-probe
''
