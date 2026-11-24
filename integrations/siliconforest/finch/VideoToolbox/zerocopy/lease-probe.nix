# SPDX-License-Identifier: MIT
let
 e=import ../../environment.nix;
 a=import ../../../cocoa/arm64.nix;
 ff=import ./ffmpeg.nix;
 user=import ../../../m1-avd/build-user.nix;
 cv=import ./corevideo.nix;
in e.pkgs.runCommand "siliconforest-video-frame-lease-probe-arm64" {} ''
 export DARWIN_SDK_ROOT=${e.sdk}
 mkdir -p $out/bin
 ${e.cc} -O2 -fno-objc-arc -fobjc-exceptions -isysroot ${e.sdk} -I${e.libsystem}/usr/include \
  -I${a.base.libobjc}/usr/include -I${a.base.foundation}/usr/include -I${a.base.corefoundation}/include \
  -I${../../CoreVideo/include} -I${a.appDeps.coregraphics}/usr/include -I${a.appDeps.onyx2d}/usr/include -I${a.src}/src/Frameworks -I${a.mesa}/usr/include -F${a.openglFramework}/System/Library/Frameworks -I${ff}/usr/include -I${./.} \
  ${./lease.m} ${./lease-probe.m} --ld-path=${e.ld} -nostdlib \
  -L${e.libsystem}/usr/lib -L${a.base.libobjc}/usr/lib -L${a.base.foundation}/usr/lib -L${a.base.corefoundation}/usr/lib \
  -F${cv}/System/Library/Frameworks -F${a.iosurface}/System/Library/Frameworks \
  -lFoundation -lobjc -lCoreFoundation -framework CoreVideo -framework IOSurface ${ff}/usr/lib/libavutil.a ${user}/lib/libsfavd.a -lSystem \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 -o $out/bin/lease-probe
''
