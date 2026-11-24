# SPDX-License-Identifier: MIT
# Local-state absence facade. No system Now Playing or media-key service.
let e=import ../environment.nix; a=import ../../cocoa/arm64.nix; headers=import ../media/headers.nix; in
e.pkgs.runCommand "siliconforest-mediaplayer-local-state-arm64" {} ''
 export DARWIN_SDK_ROOT=${e.sdk}
 F=$out/System/Library/Frameworks/MediaPlayer.framework
 A=$F/Versions/A
 mkdir -p $A/Headers $A/Resources/Source $out/bin
 CF="-O2 -fblocks -fobjc-exceptions -fno-objc-arc -isysroot ${e.sdk} -I${e.libsystem}/usr/include -I${a.base.libobjc}/usr/include -I${a.base.foundation}/usr/include -I${a.base.corefoundation}/include -I${headers} -I${a.appDeps.coregraphics}/usr/include -I${a.appDeps.onyx2d}/usr/include -I${a.src}/src/Frameworks -I${a.mesa}/usr/include -F${a.openglFramework}/System/Library/Frameworks"
 LF="--ld-path=${e.ld} -nostdlib -L${e.libsystem}/usr/lib -L${a.base.libobjc}/usr/lib -L${a.base.foundation}/usr/lib -L${a.base.corefoundation}/usr/lib -lFoundation -lCoreFoundation -lobjc -lSystem -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5"
 ${e.cc} $CF -dynamiclib ${./MediaPlayer.m} $LF -Wl,-compatibility_version,1 -Wl,-current_version,1 -Wl,-install_name,/System/Library/Frameworks/MediaPlayer.framework/Versions/A/MediaPlayer -o $A/MediaPlayer
 cp ${./MediaPlayer.h} $A/Headers/MediaPlayer.h
 cp ${./MediaPlayer.m} ${./probe.m} ${./README.md} $A/Resources/Source/
 ln -s A $F/Versions/Current
 ln -s Versions/Current/MediaPlayer $F/MediaPlayer
 ln -s Versions/Current/Headers $F/Headers
 ${e.cc} $CF -I${./.} ${./probe.m} $LF -F$out/System/Library/Frameworks -framework MediaPlayer -o $out/bin/mediaplayer-probe
''
