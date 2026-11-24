# SPDX-License-Identifier: MIT
let
  e = import ../environment.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
  toolbox = import ./build.nix;
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-audio-file-converter-tests-arm64";
  src = finch;
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    mkdir -p bin headers/AudioToolbox
    cp ${toolbox}/System/Library/Frameworks/AudioToolbox.framework/Versions/A/Headers/Generated.h headers/AudioToolbox/
    # Keep every generated file inside the test packet's working directory.
    sed -i 's#/tmp/finch-extaudiofile-test#./finch-extaudiofile-test#g' userland/tests/extaudiofile-test.c
    CF="-isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fblocks -D_DARWIN_C_SOURCE -Wno-nullability-completeness -Wno-macro-redefined -I${./include} -I$PWD/headers -I${libsystem}/usr/include -I${p.coreFoundation-arm64}/include -I${p.coreServices-arm64}/usr/include -I${e.pd.outPath}/src/Frameworks"
    LF="--ld-path=${ld} -nostdlib -isysroot ${sdk} -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -L${p.coreFoundation-arm64}/usr/lib -F${toolbox}/System/Library/Frameworks -F${p.coreServices-arm64}/System/Library/Frameworks -framework AudioToolbox -framework CoreServices -lCoreFoundation -lSystem -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5"
    for name in audioconverter-test extaudiofile-test; do
      ${cc} $CF userland/tests/$name.c $LF -o bin/$name
    done
    ${cc} $CF ${./probe.c} $LF -o bin/audio-file-probe
  '';
  installPhase = ''mkdir -p $out/bin; cp bin/* $out/bin/'';
  dontFixup = true;
  dontStrip = true;
}
