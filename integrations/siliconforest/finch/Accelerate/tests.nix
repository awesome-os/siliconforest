# SPDX-License-Identifier: MIT
let
  e = import ../environment.nix;
  inherit (e) pkgs p libsystem sdk cc ld;
  numeric = import ./build.nix { groups = [ "vDSP" "vMisc" "BLAS" "LAPACK" ]; };
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-Accelerate-bootstrap-tests-arm64";
  dontUnpack = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    ${cc} -isysroot ${sdk} -mmacosx-version-min=26.0 -O2 -I${numeric}/headers -I${libsystem}/usr/include \
      ${./probe.c} --ld-path=${ld} -nostdlib -L${numeric}/staged-bootstrap -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.0,26.0 -lvDSP -lvMisc -lBLAS -lLAPACK -lSystem -o probe
  '';
  installPhase = ''mkdir -p $out/bin; cp probe $out/bin/'';
  dontFixup = true;
  dontStrip = true;
}
