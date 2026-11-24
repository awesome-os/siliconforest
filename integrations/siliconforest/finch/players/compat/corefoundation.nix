# SPDX-License-Identifier: MIT
# Existing framework-compat source + one legacy calendar identifier; no core dylib replacement.
let e=import ../../environment.nix; a=import ../../../cocoa/arm64.nix; in
 e.pkgs.runCommand "siliconforest-vlc-corefoundation-calendar-arm64" {} ''
 export DARWIN_SDK_ROOT=${e.sdk}
 F=$out/Frameworks/CoreFoundation.framework;mkdir -p $F/Versions/A/Resources
 CF="-O2 -fblocks -fno-objc-arc -isysroot ${e.sdk} -I${e.libsystem}/usr/include -I${a.base.libobjc}/usr/include -I${a.base.foundation}/usr/include -I${a.base.corefoundation}/include -I${e.p.icuCore-arm64}/include"
 for name in compat am calendar;do
  case $name in
   compat) source=${../../../terminal-app/compat/CoreFoundation.m};;
   am) source=${../../../activity-monitor/libs/CoreFoundation-am.m};;
   calendar) source=${./calendar.m};;
  esac
  ${e.cc} $CF -c "$source" -o $name.o
 done
 ${e.cc} -dynamiclib compat.o am.o calendar.o --ld-path=${e.ld} -nostdlib -isysroot ${e.sdk} -L${e.libsystem}/usr/lib -L${a.base.libobjc}/usr/lib -L${a.base.foundation}/usr/lib -L${a.base.corefoundation}/usr/lib -L${e.p.icuCore-arm64}/usr/lib -lSystem -lobjc -licucore -Wl,-reexport_library,${a.base.foundation}/usr/lib/libFoundation.dylib -Wl,-reexport_library,${a.base.corefoundation}/usr/lib/libCoreFoundation.dylib -Wl,-compatibility_version,150 -Wl,-current_version,1338 -Wl,-install_name,/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -o $F/Versions/A/CoreFoundation
 ln -s A $F/Versions/Current;ln -s Versions/Current/CoreFoundation $F/CoreFoundation
 cp ${./calendar.m} $F/Versions/A/Resources/
''
