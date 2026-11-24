# SPDX-License-Identifier: MIT
# New unavailable-feature providers + private Foundation facade, never libFoundation.
let
 e=import ../../environment.nix;
 a=import ../../../cocoa/arm64.nix;
 swift=import ../../../terminal-app/build-swift-runtime.nix;
in e.pkgs.runCommand "siliconforest-vlc-unavailable-frameworks-arm64" {} ''
 export DARWIN_SDK_ROOT=${e.sdk}
 CF="-O2 -fblocks -fno-objc-arc -isysroot ${e.sdk} -I${e.libsystem}/usr/include -I${a.base.libobjc}/usr/include -I${a.base.foundation}/usr/include -I${a.base.corefoundation}/include -I${a.appDeps.appkit}/usr/include -I${a.appDeps.quartzcore}/usr/include -I${a.appDeps.coregraphics}/usr/include -I${a.appDeps.onyx2d}/usr/include -I${a.src}/src/Frameworks -I${a.mesa}/usr/include -F${a.openglFramework}/System/Library/Frameworks"
 LF="--ld-path=${e.ld} -nostdlib -isysroot ${e.sdk} -L${e.libsystem}/usr/lib -L${a.base.libobjc}/usr/lib -L${a.base.foundation}/usr/lib -L${a.base.corefoundation}/usr/lib -L${a.appDeps.appkit}/usr/lib -lAppKit -lFoundation -lCoreFoundation -lobjc -lSystem -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5"
 kind=0
 for name in AddressBook ScriptingBridge WebKit Sparkle;do
  kind=$((kind+1));F=$out/Frameworks/$name.framework;mkdir -p $F/Versions/A/Resources
  ${e.cc} $CF -DSF_KIND=$kind -c ${./unavailable.m} -o $name.o
  version=1.0
  if [ "$name" = Sparkle ];then version=1.6;fi
  ${e.cc} -dynamiclib $name.o $LF -Wl,-compatibility_version,$version -Wl,-current_version,$version -Wl,-install_name,/System/Library/Frameworks/$name.framework/Versions/A/$name -o $F/Versions/A/$name
  ln -s A $F/Versions/Current;ln -s A $F/Versions/C;ln -s Versions/Current/$name $F/$name
  cp ${./unavailable.m} $F/Versions/A/Resources/Source.m
 done
 F=$out/Frameworks/Foundation.framework;mkdir -p $F/Versions/A/Resources
 ${e.cc} $CF -c ${./foundation.m} -o Foundation.o
 ${e.cc} $CF -c ${./defaults.m} -o Defaults.o
 ${e.cc} $CF -c ${./calendar-operations.m} -o Calendar-operations.o
 ${e.cc} $CF -c ${./locale-operations.m} -o Locale-operations.o
 # Preserve the installed Versions/C wrapper, including its Swift overlay and
 # Activity Monitor/Terminal additions. Versions/A alone is only libFoundation.
 ${e.cc} $CF -c ${../../../terminal-app/compat/Foundation.m} -o Foundation-compat.o
 ${e.cc} $CF -c ${../../../activity-monitor/libs/Foundation-am.m} -o Foundation-am.o
 ${e.cc} -dynamiclib Foundation.o Defaults.o Calendar-operations.o Locale-operations.o Foundation-compat.o Foundation-am.o --ld-path=${e.ld} -nostdlib -isysroot ${e.sdk} -Wl,-reexport_library,${a.base.foundation}/usr/lib/libFoundation.dylib -Wl,-reexport_library,${swift}/usr/lib/swift/libswiftFoundation.dylib -L${e.libsystem}/usr/lib -L${a.base.libobjc}/usr/lib -L${a.base.corefoundation}/usr/lib -lSystem -lobjc -lCoreFoundation -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -Wl,-compatibility_version,300 -Wl,-current_version,5026.5.4 -Wl,-install_name,/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation.vlccompat -o $F/Versions/A/Foundation
 ln -s A $F/Versions/Current;ln -s A $F/Versions/C;ln -s Versions/Current/Foundation $F/Foundation
 cp ${./foundation.m} $F/Versions/A/Resources/Source.m
''
