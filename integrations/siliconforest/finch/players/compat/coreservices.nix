# SPDX-License-Identifier: MIT
# Keep the installed CoreServices family, add real registry/catalog operations.
let
 e=import ../../environment.nix;
 a=import ../../../cocoa/arm64.nix;
 original=import ../../../macos-compat/build-coreservices.nix;
 source=e.pkgs.runCommand "siliconforest-coreservices-vlc-source" {} ''
  cp -R ${original.src} $out;chmod -R u+w $out
  D=$out/src/Libraries/CoreServices
  cp ${./catalog.h} $D/catalog.h
  cat ${./handlers.inc} >> $D/LaunchServices.c
  cat ${./catalog.c} >> $D/FileManager.c
 '';
 actual=original.overrideAttrs(old:{src=source;name="siliconforest-coreservices-vlc-metadata-arm64";});
in e.pkgs.runCommand "siliconforest-vlc-coreservices-prefix-arm64" {} ''
 export DARWIN_SDK_ROOT=${e.sdk}
 F=$out/Frameworks/CoreServices.framework;mkdir -p $F/Versions/A/Resources
 cp ${actual}/System/Library/Frameworks/CoreServices.framework/Versions/A/CoreServices $F/Versions/A/CoreServices.pd
 chmod u+w $F/Versions/A/CoreServices.pd
 ${e.p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-install_name_tool -id @loader_path/CoreServices.pd $F/Versions/A/CoreServices.pd
 ${e.pkgs.python3}/bin/python3 ${../../../m1-mesa-asahi/tools/cs-rehash.py} $F/Versions/A/CoreServices.pd
 ${e.cc} -O2 -dynamiclib -isysroot ${e.sdk} -I${a.base.corefoundation}/include -I${e.libsystem}/usr/include ${../../../terminal-app/compat/CoreServices.c} ${../../../activity-monitor/libs/CoreServices-am.c} --ld-path=${e.ld} -nostdlib -L${e.libsystem}/usr/lib -L${a.base.corefoundation}/usr/lib -lCoreFoundation -lSystem -Wl,-reexport_library,$F/Versions/A/CoreServices.pd -Wl,-compatibility_version,1 -Wl,-current_version,1226 -Wl,-install_name,/System/Library/Frameworks/CoreServices.framework/Versions/A/CoreServices.sfcompat -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -o $F/Versions/A/CoreServices
 ln -s A $F/Versions/Current;ln -s A $F/Versions/C;ln -s Versions/Current/CoreServices $F/CoreServices
 cp ${./catalog.c} ${./catalog.h} ${./handlers.inc} $F/Versions/A/Resources/
''
