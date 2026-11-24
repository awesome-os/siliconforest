# SPDX-License-Identifier: MIT
# Default target is the whole Finch Accelerate tree. groups permits a staged
# bootstrap while we author the missing open declarations; never install that as whole.
{ groups ? [ "vDSP" "vMisc" "BLAS" "LAPACK" "vImage" "Quadrature" "LinearAlgebra" "SparseBLAS" "Sparse" ] }:
let
  e = import ../environment.nix;
  inherit (e) pkgs p finch libsystem sdk cc ld;
  colorSync = import ../ColorSync/build.nix;
  selected = pkgs.lib.unique (pkgs.lib.concatMap (g:
    if g == "LAPACK" then [ "BLAS" g ] else if g == "Sparse" then [ "SparseBLAS" g ] else [ g ]) groups);
  whole = pkgs.lib.all (g: builtins.elem g selected)
    [ "vDSP" "vMisc" "BLAS" "LAPACK" "vImage" "Quadrature" "LinearAlgebra" "SparseBLAS" "Sparse" ];
in pkgs.stdenv.mkDerivation {
  name = "siliconforest-finch-Accelerate-arm64";
  src = finch;
  patches = [ ./patches/0001-vdsp-nearest-int16-saturation.patch ./patches/0002-local-la-lifetime.patch ./patches/0003-quadrature-runtime-workspace.patch ./patches/0005-vimage-named-display-p3.patch ];
  nativeBuildInputs = [ pkgs.python3 ];
  dontConfigure = true;
  buildPhase = ''
    export DARWIN_SDK_ROOT=${sdk}
    cp -r ${./include} headers
    chmod -R u+w headers
    python3 ${./sparse-overloads.py} headers/vecLib/Sparse/Solve.h
    CF="-isysroot ${sdk} -mmacosx-version-min=26.5 -O2 -fblocks -std=c17 -D_DARWIN_C_SOURCE -DSF_ACCELERATE_BUILD -I$PWD/headers -I${libsystem}/usr/include -I${p.libobjc-arm64}/usr/include -I${p.coreFoundation-arm64}/include -I${p.foundation-arm64}/usr/include -I${p.coregraphics-arm64}/usr/include -I${p.onyx2d-arm64}/usr/include -I${e.pd.outPath}/src/Frameworks"
    LF="--ld-path=${ld} -nostdlib -isysroot ${sdk} -L${libsystem}/usr/lib -L${libsystem}/usr/lib/system -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libsystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5"
    mkdir -p objects libs
    for group in ${pkgs.lib.concatStringsSep " " selected}; do
      mkdir -p objects/$group
      for s in userland/Accelerate/$group/*.c; do
        ${cc} $CF -DSF_COMPONENT_$group -DSF_ACCELERATE_PREPROCESS -E -P "$s" -o objects/$group/$(basename "$s" .c).i
      done
      python3 ${./declarations.py} ${../tools/c_functions.py} headers/Accelerate/$group-generated.h objects/$group/*.i
      if [ "$group" = BLAS ]; then
        python3 ${./blas-declarations.py} headers/Accelerate/BLAS-generated.h objects/BLAS/abi-input
        python3 userland/Accelerate/BLAS/gen-abi.py objects/BLAS/abi-input objects/BLAS/abi-input/symbols > objects/BLAS/abi.c
        ${cc} $CF -c objects/BLAS/abi.c -o objects/BLAS/abi.o
      fi
      for s in userland/Accelerate/$group/*.c; do
        ${cc} $CF -DSF_COMPONENT_$group -c "$s" -o objects/$group/$(basename "$s" .c).o
      done
      if [ "$group" = LinearAlgebra ]; then
        ${cc} $CF -fno-objc-arc -Iuserland/Accelerate/LinearAlgebra -c ${./la-object.m} -o objects/$group/object.o
      fi
      if [ "$group" = vMisc ]; then
        ${cc} $CF -fno-builtin -c ${./math-compat.c} -o objects/$group/math-compat.o
      fi
      ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-nm -g -U objects/$group/*.o | awk '{print $NF}' | sort -u | sed -n '/^_/p' | sed '/^_finch_/d; /^_sf_sparse_/d; /__Z[0-9]*sf_sparse_/d; /___copy_helper_block_/d; /___destroy_helper_block_/d; /^_la_object_/d; /^_vimage_/d; /^_sparse_report$/d; /^___\(sin\|cos\|tan\)pi/d' > objects/$group/exports
      EXTRA=""
      if [ "$group" = LAPACK ]; then
        EXTRA="-L$PWD/libs -Wl,-reexport_library,$PWD/libs/libBLAS.dylib"
      fi
      if [ "$group" = Sparse ]; then
        EXTRA="-L$PWD/libs -lSparseBLAS"
      fi
      if [ "$group" = LinearAlgebra ]; then
        EXTRA="-L${p.foundation-arm64}/usr/lib -L${p.coreFoundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib -lFoundation -lCoreFoundation -lobjc"
      fi
      if [ "$group" = vImage ]; then
        EXTRA="-L${p.coreFoundation-arm64}/usr/lib -L${p.coregraphics-arm64}/usr/lib -L${p.onyx2d-arm64}/usr/lib -L${p.foundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib -F${colorSync}/System/Library/Frameworks -framework ColorSync -lCoreGraphics -lOnyx2D -lFoundation -lCoreFoundation -lobjc"
      fi
      ID=/System/Library/Frameworks/Accelerate.framework/Versions/A/Frameworks/vecLib.framework/Versions/A/lib$group.dylib
      VERSION=1126.100.7
      if [ "$group" = vImage ]; then
        ID=/System/Library/Frameworks/Accelerate.framework/Versions/A/Frameworks/vImage.framework/Versions/A/vImage
        VERSION=632.100.6
      fi
      ${cc} -dynamiclib objects/$group/*.o $LF $EXTRA -Wl,-exported_symbols_list,objects/$group/exports ${p.compiler-rt-arm64}/lib/libcompiler_rt.a -install_name "$ID" -current_version "$VERSION" -compatibility_version 1 -lSystem -o libs/lib$group.dylib
    done
    ${pkgs.lib.optionalString whole ''
      printf '/* Re-export-only umbrella. */\n' > umbrella.c
      ${cc} $CF -c umbrella.c -o umbrella.o
      REEXPORT=""
      REEXPORT_MAP=""
      for group in vDSP vMisc BLAS LAPACK Quadrature LinearAlgebra SparseBLAS Sparse; do
        REEXPORT="$REEXPORT -Wl,-reexport_library,$PWD/libs/lib$group.dylib"
        REEXPORT_MAP="$REEXPORT_MAP -Wl,-dylib_file,/System/Library/Frameworks/Accelerate.framework/Versions/A/Frameworks/vecLib.framework/Versions/A/lib$group.dylib:$PWD/libs/lib$group.dylib"
      done
      ${cc} -dynamiclib umbrella.o $LF -L$PWD/libs -L${p.foundation-arm64}/usr/lib -L${p.coreFoundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib $REEXPORT $REEXPORT_MAP -lSystem \
        -install_name /System/Library/Frameworks/Accelerate.framework/Versions/A/Frameworks/vecLib.framework/Versions/A/vecLib -current_version 1126.100.7 -compatibility_version 1 -o libs/vecLib
      ${cc} -dynamiclib umbrella.o $LF -L$PWD/libs -L${p.foundation-arm64}/usr/lib -L${p.coreFoundation-arm64}/usr/lib -L${p.libobjc-arm64}/usr/lib \
        -Wl,-reexport_library,$PWD/libs/vecLib -Wl,-reexport_library,$PWD/libs/libvImage.dylib $REEXPORT_MAP -lSystem \
        -install_name /System/Library/Frameworks/Accelerate.framework/Versions/A/Accelerate -current_version 4 -compatibility_version 1 -o libs/Accelerate
    ''}
  '';
  installPhase = ''
    mkdir -p $out/staged-bootstrap $out/usr/share/licenses/finch-Accelerate
    cp libs/*.dylib $out/staged-bootstrap/
    cp -r headers $out/
    cp ${finch}/LICENSE* $out/usr/share/licenses/finch-Accelerate/
    ${pkgs.lib.optionalString whole ''
      fw=$out/System/Library/Frameworks/Accelerate.framework
      vec=$fw/Versions/A/Frameworks/vecLib.framework
      image=$fw/Versions/A/Frameworks/vImage.framework
      mkdir -p $fw/Versions/A/Resources $vec/Versions/A $image/Versions/A/Headers
      cp libs/Accelerate $fw/Versions/A/
      cp libs/vecLib $vec/Versions/A/
      for group in vDSP vMisc BLAS LAPACK Quadrature LinearAlgebra SparseBLAS Sparse; do cp libs/lib$group.dylib $vec/Versions/A/; done
      cp libs/libvImage.dylib $image/Versions/A/vImage
      cp -r headers/Accelerate $fw/Versions/A/Headers
      cp -r headers/vecLib $vec/Versions/A/Headers
      printf '#include <Accelerate/Image.h>\n#include <Accelerate/vImage-generated.h>\n' > $image/Versions/A/Headers/vImage.h
      for item in Accelerate vecLib vImage; do
        case $item in
          Accelerate) root=$fw; identifier=com.apple.Accelerate; version=4; short=1.11;;
          vecLib) root=$vec; identifier=com.apple.Accelerate.vecLib; version=1126.100.7; short=3.11;;
          vImage) root=$image; identifier=com.apple.Accelerate.vImage; version=632.100.6; short=2.1;;
        esac
        mkdir -p $root/Versions/A/Resources
        ln -s A $root/Versions/Current
        ln -s Versions/Current/$item $root/$item
        ln -s Versions/Current/Headers $root/Headers
        cat > $root/Versions/A/Resources/Info.plist <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict><key>CFBundleIdentifier</key><string>$identifier</string><key>CFBundleExecutable</key><string>$item</string><key>CFBundleVersion</key><string>$version</string><key>CFBundleShortVersionString</key><string>$short</string></dict></plist>
PLIST
      done
      ln -s Versions/Current/Frameworks $fw/Frameworks
      cp -r $out/usr/share/licenses/finch-Accelerate $fw/Versions/A/Resources/Licenses
    ''}
  '';
  dontFixup = true;
  dontStrip = true;
}
