# SPDX-License-Identifier: MIT
let
  e = import ../environment.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
  numeric = import ./build.nix { groups = [ "vDSP" "vMisc" "BLAS" "LAPACK" ]; };
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-Accelerate-comparison-tests-arm64";
  src = finch;
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    mkdir bin
    for name in vdsp vmisc blas; do
      ${cc} -isysroot ${sdk} -mmacosx-version-min=26.0 -O2 \
        -DSF_COMPONENT_vDSP -DSF_COMPONENT_vMisc -DSF_COMPONENT_BLAS -DSF_COMPONENT_LAPACK \
        -I${numeric}/headers -I${libsystem}/usr/include userland/tests/$name-test.c \
        --ld-path=${ld} -nostdlib -L${numeric}/staged-bootstrap -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system \
        -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib \
        -Wl,-platform_version,macos,26.0,26.0 -lvDSP -lvMisc -lBLAS -lLAPACK -lSystem -o bin/$name-test
    done
  '';
  installPhase = ''mkdir -p $out/bin; cp bin/* $out/bin/'';
  dontFixup = true;
  dontStrip = true;
}
