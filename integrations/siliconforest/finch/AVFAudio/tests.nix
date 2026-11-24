# SPDX-License-Identifier: MIT
let
  e = import ../environment.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
  av = import ./build.nix;
  toolbox = import ../AudioToolbox/build.nix;
  cm = import ../CoreMedia/build.nix;
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-AVFAudio-file-converter-tests-arm64";
  src = finch;
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    mkdir -p bin headers/AudioToolbox
    cp ${toolbox}/System/Library/Frameworks/AudioToolbox.framework/Versions/A/Headers/Generated.h headers/AudioToolbox/
    sed -i 's#/tmp/finch-avaudio-file-#./finch-avaudio-file-#g' userland/tests/avaudio-file-test.m
    CF="-isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fblocks -fobjc-exceptions -fno-objc-arc -D_DARWIN_C_SOURCE -Wno-nullability-completeness -Wno-macro-redefined -I${./include} -I${../AudioToolbox/include} -I$PWD/headers -I${p.foundation-arm64}/usr/include -I${p.coreFoundation-arm64}/include -I${p.libobjc-arm64}/usr/include -I${libsystem}/usr/include -I${p.coreServices-arm64}/usr/include -I${e.pd.outPath}/src/Frameworks"
    LF="--ld-path=${ld} -nostdlib -isysroot ${sdk} -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -L${p.foundation-arm64}/usr/lib -L${p.coreFoundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib -F${av}/System/Library/Frameworks -F${toolbox}/System/Library/Frameworks -F${cm}/System/Library/Frameworks -framework AVFAudio -framework AudioToolbox -framework CoreMedia -lFoundation -lCoreFoundation -lobjc -lSystem -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5"
    for name in avaudio-buffer-test avaudio-compressed-buffer-test avaudio-converter-test avaudio-file-test; do
      ${cc} $CF userland/tests/$name.m $LF -o bin/$name
    done
  '';
  installPhase = ''mkdir -p $out/bin; cp bin/* $out/bin/'';
  dontFixup = true;
  dontStrip = true;
}
