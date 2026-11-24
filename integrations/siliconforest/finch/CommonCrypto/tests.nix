# SPDX-License-Identifier: MIT
let
  e = import ../environment.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
  common = import ./build.nix;
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-commoncrypto-tests-arm64";
  src = finch;
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    mkdir bin
    for n in commoncrypto-compare commoncrypto-keys-compare; do
      sed 's@/usr/lib/system/libcommonCrypto.dylib@/usr/lib/libSystem.B.dylib@g' userland/corecrypto/tests/$n.c > userland/corecrypto/tests/sf-$n.c
      ${cc} -isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fblocks \
        -I${common}/include/headers -I${common}/include -I${libsystem}/usr/include \
        -Wno-deprecated-declarations userland/corecrypto/tests/sf-$n.c \
        --ld-path=${ld} -nostdlib -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system \
        -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib \
        -Wl,-platform_version,macos,26.5,26.5 -lSystem -o bin/$n
    done
  '';
  installPhase = ''mkdir -p $out/bin; cp bin/* $out/bin/'';
  dontFixup = true;
  dontStrip = true;
}
