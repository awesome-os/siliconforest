# SPDX-License-Identifier: MIT
let e=import ../environment.nix; in
e.pkgs.runCommand "siliconforest-coremediaio-unavailable-stage-arm64" {} ''
 export DARWIN_SDK_ROOT=${e.sdk}
 F=$out/System/Library/Frameworks/CoreMediaIO.framework;mkdir -p $F/Versions/A/Resources/Source
 ${e.cc} -O2 -dynamiclib -isysroot ${e.sdk} -I${e.libsystem}/usr/include ${./CoreMediaIO.c} --ld-path=${e.ld} -nostdlib -L${e.libsystem}/usr/lib -lSystem \
 -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
 -Wl,-platform_version,macos,26.5,26.5 -Wl,-compatibility_version,1 -Wl,-current_version,1 -Wl,-install_name,/System/Library/Frameworks/CoreMediaIO.framework/Versions/A/CoreMediaIO -o $F/Versions/A/CoreMediaIO
 cp ${./CoreMediaIO.c} $F/Versions/A/Resources/Source/CoreMediaIO.c
 ln -s A $F/Versions/Current;ln -s Versions/Current/CoreMediaIO $F/CoreMediaIO
''
