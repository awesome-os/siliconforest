# SPDX-License-Identifier: MIT
let
  e = import ../environment.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
  math = import ./build.nix { groups = [ "LinearAlgebra" "Quadrature" ]; };
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-LA-Quadrature-tests-arm64";
  src = finch;
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    mkdir bin
    for name in linearalgebra quadrature; do
      ${cc} -isysroot ${sdk} -mmacosx-version-min=26.0 -O2 -fblocks \
        -DSF_COMPONENT_LinearAlgebra -DSF_COMPONENT_Quadrature \
        -I${math}/headers -I${libsystem}/usr/include -I${p.libobjc-arm64}/usr/include userland/tests/$name-test.c \
        --ld-path=${ld} -nostdlib -L${math}/staged-bootstrap -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -L${p.libobjc-arm64}/usr/lib \
        -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib \
        -Wl,-platform_version,macos,26.0,26.0 -lLinearAlgebra -lQuadrature -lobjc -lSystem -o bin/$name-test
    done
  '';
  installPhase = ''mkdir -p $out/bin; cp bin/* $out/bin/'';
  dontFixup = true;
  dontStrip = true;
}
