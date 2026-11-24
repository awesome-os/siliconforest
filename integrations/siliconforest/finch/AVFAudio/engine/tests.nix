# SPDX-License-Identifier: MIT
let
  e=import ../../environment.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
  toolbox=import ../../AudioToolbox/codecs.nix;
  engine=import ./build.nix;
  coreMedia=import ../../CoreMedia/build.nix;
in pkgs.stdenv.mkDerivation {
  name="siliconforest-finch-AVFAudio-codecs-engine-tests-arm64";
  src=finch;
  dontConfigure=true;
  buildPhase=''
    export DARWIN_SDK_ROOT=${sdk}
    CF="-isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fblocks -fobjc-exceptions -fno-objc-arc -D_DARWIN_C_SOURCE -Wno-nullability-completeness -Wno-macro-redefined -I${./include} -I${../include} -I${toolbox}/System/Library/Frameworks/AudioToolbox.framework/Versions/A/Headers/.. -I${../../AudioToolbox/include} -I${p.foundation-arm64}/usr/include -I${p.coreFoundation-arm64}/include -I${p.libobjc-arm64}/usr/include -I${libsystem}/usr/include -I${p.coreServices-arm64}/usr/include -I${e.pd.outPath}/src/Frameworks"
    mkdir -p headers/AudioToolbox bin
    cp ${toolbox}/System/Library/Frameworks/AudioToolbox.framework/Versions/A/Headers/Generated.h headers/AudioToolbox/
    CF="$CF -I$PWD/headers"
    LF="--ld-path=${ld} -nostdlib -isysroot ${sdk} -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -L${p.foundation-arm64}/usr/lib -L${p.coreFoundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib -F${engine}/System/Library/Frameworks -framework AVFAudio -F${toolbox}/System/Library/Frameworks -framework AudioToolbox -Wl,-dylib_file,@loader_path/AudioToolbox.output-sf:${../../AudioToolbox/codec/retained-provider.tbd} -F${coreMedia}/System/Library/Frameworks -framework CoreMedia -lFoundation -lCoreFoundation -lobjc -lSystem -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5"
    ${cc} $CF userland/tests/avaudio-engine-test.m $LF -o bin/avaudio-engine-test
    ${cc} $CF ${./file-converter-probe.m} $LF -o bin/avaudio-file-codecs-probe
  '';
  installPhase=''mkdir -p $out/bin;cp bin/* $out/bin/'';
  dontFixup=true;dontStrip=true;
}
