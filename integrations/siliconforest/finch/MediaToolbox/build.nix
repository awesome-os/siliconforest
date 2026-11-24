# SPDX-License-Identifier: MIT
# Five metadata operations and the documented void reader opt-in diagnostic.
# FigAssetCreateWithURL remains absent rather than guessing its private ABI.
let e=import ../environment.nix; a=import ../../cocoa/arm64.nix; cm=import ../CoreMedia/build.nix; headers=import ../media/headers.nix; in
e.pkgs.runCommand "siliconforest-mediatoolbox-format-names-stage-arm64" {} ''
 export DARWIN_SDK_ROOT=${e.sdk}
 F=$out/System/Library/Frameworks/MediaToolbox.framework
 mkdir -p $F/Versions/A/Resources/Source
 ${e.cc} -O2 -fblocks -dynamiclib -isysroot ${e.sdk} -I${e.libsystem}/usr/include \
  -I${e.p.coreFoundation-arm64}/include -I${headers} -I${a.appDeps.coregraphics}/usr/include -I${a.appDeps.onyx2d}/usr/include -I${a.src}/src/Frameworks -I${a.mesa}/usr/include -F${a.openglFramework}/System/Library/Frameworks ${./FormatNames.c} ${./Metadata.c} ${./ProfessionalReaders.c} \
  --ld-path=${e.ld} -nostdlib -L${e.libsystem}/usr/lib -L${e.p.coreFoundation-arm64}/usr/lib \
  -F${cm}/System/Library/Frameworks -framework CoreMedia -lCoreFoundation -lSystem -Wl,-compatibility_version,1 -Wl,-current_version,1 \
  -Wl,-install_name,/System/Library/Frameworks/MediaToolbox.framework/Versions/A/MediaToolbox \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 -o $F/Versions/A/MediaToolbox
 cp ${./FormatNames.c} ${./Metadata.c} ${./ProfessionalReaders.c} $F/Versions/A/Resources/Source/
 ln -s A $F/Versions/Current;ln -s Versions/Current/MediaToolbox $F/MediaToolbox
''
