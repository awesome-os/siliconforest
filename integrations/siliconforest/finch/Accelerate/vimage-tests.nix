# SPDX-License-Identifier: MIT
let
  e=import ../environment.nix;
  a=import ../../cocoa/arm64.nix;
  graphics=import ../../cocoa/patched.nix;
  inherit(e) pkgs p finch libsystem sdk cc ld;
  whole=import ./installed-graphics.nix;
  cv=import ../CoreVideo/sf-displaylink.nix;
  headers=import ../media/headers.nix;
in pkgs.stdenv.mkDerivation {
  name="siliconforest-finch-vimage-comparison-test-arm64";
  src=finch;dontConfigure=true;
  buildPhase=''
    export DARWIN_SDK_ROOT=${sdk}
    MAPS=""
    for group in vDSP vMisc BLAS LAPACK Quadrature LinearAlgebra SparseBLAS Sparse; do
      file=/System/Library/Frameworks/Accelerate.framework/Versions/A/Frameworks/vecLib.framework/Versions/A/lib$group.dylib
      MAPS="$MAPS -Wl,-dylib_file,$file:${whole}$file"
    done
    ${cc} -isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fblocks -D_DARWIN_C_SOURCE -Wno-nullability-completeness -Wno-macro-redefined -DSF_COMPONENT_vImage \
      -I${whole}/headers -I${headers} -I${graphics.coregraphics}/usr/include -I${graphics.onyx2d}/usr/include -I${p.coreFoundation-arm64}/include -I${p.libobjc-arm64}/usr/include -I${libsystem}/usr/include -I${e.pd.outPath}/src/Frameworks -I${a.mesa}/usr/include -F${a.openglFramework}/System/Library/Frameworks \
      userland/tests/vimage-test.c --ld-path=${ld} -nostdlib -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -L${p.coreFoundation-arm64}/usr/lib -L${graphics.coregraphics}/usr/lib -L${graphics.onyx2d}/usr/lib \
      -F${whole}/System/Library/Frameworks -F${cv}/System/Library/Frameworks -framework Accelerate -framework CoreVideo $MAPS -lCoreGraphics -lCoreFoundation -lSystem \
      -Wl,-dylib_file,/System/Library/Frameworks/Accelerate.framework/Versions/A/Frameworks/vecLib.framework/Versions/A/vecLib:${whole}/System/Library/Frameworks/Accelerate.framework/Versions/A/Frameworks/vecLib.framework/Versions/A/vecLib \
      -Wl,-dylib_file,/System/Library/Frameworks/Accelerate.framework/Versions/A/Frameworks/vImage.framework/Versions/A/vImage:${whole}/System/Library/Frameworks/Accelerate.framework/Versions/A/Frameworks/vImage.framework/Versions/A/vImage \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -o vimage-test
  '';
  installPhase=''mkdir -p $out/bin;cp vimage-test $out/bin/'';
  dontFixup=true;dontStrip=true;
}
