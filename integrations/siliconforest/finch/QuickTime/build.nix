# SPDX-License-Identifier: MIT
# Eight private-framework absence providers. Linux/Nix + PD OSS declarations;
# no Apple SDK or macOS compiler. FeatureFlags has compiler-emitted Swift ABI.
let
 e=import ../environment.nix;
 a=import ../../cocoa/arm64.nix;
 cross=import ../Network/toolchain.nix;
 runtime=import ../../terminal-app/build-swift-runtime.nix;
 headers=import ../media/headers.nix;
in e.pkgs.runCommand "siliconforest-quicktime-private-frameworks-arm64" {} ''
 export DARWIN_SDK_ROOT=${e.sdk}
 CF="-O2 -fblocks -fobjc-exceptions -fno-objc-arc -isysroot ${e.sdk} -I${e.libsystem}/usr/include -I${a.base.libobjc}/usr/include -I${a.base.foundation}/usr/include -I${a.base.corefoundation}/include -I${headers} -I${a.appDeps.coregraphics}/usr/include -I${a.appDeps.onyx2d}/usr/include -I${a.src}/src/Frameworks -I${a.mesa}/usr/include -F${a.openglFramework}/System/Library/Frameworks"
 LF="--ld-path=${e.ld} -nostdlib -isysroot ${e.sdk} -L${e.libsystem}/usr/lib -L${a.base.libobjc}/usr/lib -L${a.base.foundation}/usr/lib -L${a.base.corefoundation}/usr/lib -lFoundation -lCoreFoundation -lobjc -lSystem -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5"
 root=$out/System/Library/PrivateFrameworks
 for spec in TCC:1 MediaRemote:2 PerformanceAnalysis:3; do
  name=''${spec%:*};kind=''${spec#*:};F=$root/$name.framework;mkdir -p $F/Versions/A/Resources/Source
  ${e.cc} $CF -DSF_KIND=$kind -dynamiclib ${./Unavailable.c} $LF -Wl,-compatibility_version,1 -Wl,-current_version,1 -Wl,-install_name,/System/Library/PrivateFrameworks/$name.framework/Versions/A/$name -o $F/Versions/A/$name
  cp ${./Unavailable.c} $F/Versions/A/Resources/Source/Unavailable.c
 done
 for spec in PhotosFormats:1 GeoKit:2 MobileAsset:3 MonitorPanel:4; do
  name=''${spec%:*};kind=''${spec#*:};F=$root/$name.framework;mkdir -p $F/Versions/A/Resources/Source
  ${e.cc} $CF -DSF_KIND=$kind -dynamiclib ${./Objects.m} $LF -Wl,-compatibility_version,1 -Wl,-current_version,1 -Wl,-install_name,/System/Library/PrivateFrameworks/$name.framework/Versions/A/$name -o $F/Versions/A/$name
  cp ${./Objects.m} $F/Versions/A/Resources/Source/Objects.m
 done
 F=$root/FeatureFlags.framework;mkdir -p $F/Versions/A/Resources/Source $F/Versions/A/Modules/FeatureFlags.swiftmodule
 ${cross}/swiftc -module-cache-path $TMPDIR/modules -parse-as-library -enable-library-evolution -module-name FeatureFlags -emit-object -o FeatureFlags.o ${./FeatureFlags.swift}
 ${cross}/swiftc -module-cache-path $TMPDIR/modules -parse-as-library -enable-library-evolution -module-name FeatureFlags -emit-module -emit-module-path $F/Versions/A/Modules/FeatureFlags.swiftmodule/arm64-apple-macos.swiftmodule ${./FeatureFlags.swift}
 ${e.cc} -dynamiclib FeatureFlags.o --ld-path=${e.ld} -nostdlib -isysroot ${e.sdk} -L${runtime}/usr/lib/swift -L${e.libsystem}/usr/lib -lswiftCore -lSystem \
   -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -Wl,-rpath,/usr/lib/swift \
   -Wl,-compatibility_version,1 -Wl,-current_version,1 -Wl,-install_name,/System/Library/PrivateFrameworks/FeatureFlags.framework/Versions/A/FeatureFlags -o $F/Versions/A/FeatureFlags
 cp ${./FeatureFlags.swift} $F/Versions/A/Resources/Source/FeatureFlags.swift
 mkdir -p $F/Versions/A/Resources/Licenses
 cp ${e.finch}/LICENSE* $F/Versions/A/Resources/Licenses/
 for F in $root/*.framework;do
  name=$(basename "$F" .framework)
  ln -s A $F/Versions/Current;ln -s Versions/Current/$name $F/$name
 done
''
