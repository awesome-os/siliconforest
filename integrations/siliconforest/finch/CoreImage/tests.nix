# SPDX-License-Identifier: MIT
# CoreImage CPU recipes and built-in filters; no GPU backend or device access.
let
  e = import ../environment.nix;
  a = import ../../cocoa/arm64.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
  headers = import ../media/headers.nix;
  coreimage = import ./build.nix;
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-CoreImage-cpu-tests-arm64";
  src = finch;
  patches = [ ./patches/0003-software-comparison-test.patch ];
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    mkdir objects
    CF="-isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fblocks -fobjc-exceptions -fno-objc-arc -Wno-objc-property-no-attribute -D_DARWIN_C_SOURCE -Wno-nullability-completeness -Wno-macro-redefined -Wno-incomplete-implementation -I${./include} -I${headers} -I${p.foundation-arm64}/usr/include -I${p.coreFoundation-arm64}/include -I${p.libobjc-arm64}/usr/include -I${libsystem}/usr/include -I${p.coreServices-arm64}/usr/include -I${p.coregraphics-arm64}/usr/include -I${p.onyx2d-arm64}/usr/include -I${e.pd.outPath}/src/Frameworks -I${a.mesa}/usr/include -F${a.openglFramework}/System/Library/Frameworks"
    ${cc} $CF ${./probe.m} --ld-path=${ld} -nostdlib -isysroot ${sdk} -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -L${p.foundation-arm64}/usr/lib -L${p.coreFoundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib -L${p.coregraphics-arm64}/usr/lib -L${p.onyx2d-arm64}/usr/lib -F${coreimage}/System/Library/Frameworks -framework CoreImage -lCoreGraphics -lFoundation -lCoreFoundation -lobjc -lSystem -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -o coreimage-cpu-probe
    ${cc} $CF userland/tests/coreimage-test.m --ld-path=${ld} -nostdlib -isysroot ${sdk} -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -L${p.foundation-arm64}/usr/lib -L${p.coreFoundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib -L${p.coregraphics-arm64}/usr/lib -L${p.onyx2d-arm64}/usr/lib -F${coreimage}/System/Library/Frameworks -framework CoreImage -lCoreGraphics -lFoundation -lCoreFoundation -lobjc -lSystem -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -o coreimage-test
  '';
  installPhase = ''mkdir -p $out/bin; cp coreimage-cpu-probe coreimage-test $out/bin/'';
  dontFixup = true;
  dontStrip = true;
}
