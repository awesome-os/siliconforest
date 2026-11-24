# SPDX-License-Identifier: MIT
# Objective-C model/store/context implementation, including SQLite. No Swift overlay.
let
  e = import ../environment.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
  sqlite = p.sqlite-arm64;
  foundation = import ../../terminal-app/build-foundation.nix;
  coredata = import ./build.nix;
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-CoreData-sqlite-tests-arm64";
  dontUnpack = true;
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    mkdir objects
    cp ${finch}/userland/tests/coredata-hash-test.m coredata-hash-test.m
    sed -i '/@autoreleasepool {/a\      setvbuf(stdout, NULL, _IOLBF, 0);' coredata-hash-test.m
    CF="-isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fblocks -fobjc-exceptions -fno-objc-arc -D_DARWIN_C_SOURCE -Wno-nullability-completeness -Wno-macro-redefined -Wno-incomplete-implementation -I${./include} -I${foundation}/usr/include -I${p.coreFoundation-arm64}/include -I${p.libobjc-arm64}/usr/include -I${libsystem}/usr/include -I${sqlite}/include -I${e.pd.outPath}/src/Frameworks"
    ${cc} $CF ${./probe.m} --ld-path=${ld} -nostdlib -isysroot ${sdk} -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -L${foundation}/usr/lib -L${p.coreFoundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib -L${sqlite}/lib -Wl,-force_load,${sqlite}/lib/libsqlite3.a -F${coredata}/System/Library/Frameworks -framework CoreData -lFoundation -lCoreFoundation -lobjc -lSystem -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -o coredata-sqlite-probe
    ${cc} $CF coredata-hash-test.m --ld-path=${ld} -nostdlib -isysroot ${sdk} -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -L${foundation}/usr/lib -L${p.coreFoundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib -L${sqlite}/lib -Wl,-force_load,${sqlite}/lib/libsqlite3.a -F${coredata}/System/Library/Frameworks -framework CoreData -lFoundation -lCoreFoundation -lobjc -lSystem -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -o coredata-hash-test
  '';
  installPhase = ''mkdir -p $out/bin; cp coredata-sqlite-probe coredata-hash-test $out/bin/'';
  dontFixup = true;
  dontStrip = true;
}
