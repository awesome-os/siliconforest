# SPDX-License-Identifier: MIT
let e=import ../environment.nix;inherit(e) pkgs cc ld sdk libsystem;
in pkgs.runCommand "siliconforest-vlc-legacy-error-table-arm64" {} ''
 export DARWIN_SDK_ROOT=${sdk}
 mkdir -p $out/lib
 ${cc} -dynamiclib -isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fno-stack-protector -I${libsystem}/usr/include ${./system-compat.c} --ld-path=${ld} -nostdlib -L${libsystem}/usr/lib -Wl,-reexport_library,${libsystem}/usr/lib/libSystem.B.dylib -lSystem -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -install_name @loader_path/libSFVLCSystem.dylib -o $out/lib/libSFVLCSystem.dylib
''
