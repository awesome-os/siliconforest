# SPDX-License-Identifier: MIT
let e=import ../environment.nix;in
e.pkgs.runCommand "siliconforest-firefox-owned-view-state-arm64" {} ''
 export DARWIN_SDK_ROOT=${e.sdk}
 mkdir -p $out/bin $out/lib
 ${e.cc} -O2 -fno-objc-arc -fobjc-exceptions -dynamiclib -isysroot ${e.sdk} \
  -I${e.libsystem}/usr/include -I${e.p.libobjc-arm64}/usr/include \
  ${./view-state-observer.m} --ld-path=${e.ld} -nostdlib -L${e.libsystem}/usr/lib -L${e.p.libobjc-arm64}/usr/lib \
  -lobjc -lSystem -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 -install_name @rpath/SFFirefoxViewState.dylib -o $out/lib/SFFirefoxViewState.dylib
 ${e.cc} -O2 -isysroot ${e.sdk} -I${e.libsystem}/usr/include \
  ${./view-state-control.c} --ld-path=${e.ld} -nostdlib -L${e.libsystem}/usr/lib -lSystem \
  -Wl,-needed_library,$out/lib/SFFirefoxViewState.dylib -Wl,-rpath,@executable_path/../lib \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 -o $out/bin/view-state-control
''
