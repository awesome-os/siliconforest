# SPDX-License-Identifier: MIT
# Darling's Indium (0BSD: libindium, libiridium, libindium_kit) cross-built for arm64 Darwin against our
# Vulkan (the loader of ../m1-vulkan, opened as libvulkan.1.dylib) and the SiliconForest flake's LLVM 21
# (Iridium dlopens <prefix>/lib/libLLVM.dylib), plus Indium's own tests that need no window:
#   bin/indium-test-basic-compute       add.metallib (in Indium's test suite) through Iridium
#   bin/indium-test-basic-compute-mslc  the same kernel as SPIR-V made by mslc (no LLVM needed)
#   bin/indium-test-device-info
# Built with the compiler directly (Indium's CMake asks llvm-config of the host for the run-time library).
{ prefix ? "/private/var/db/sfmetal" }:
let
  c = import ./common.nix;
  inherit (c) pkgs p triple sdkRoot;
  tc = p.arm64-cross-toolchain;
  libSystem = p.libsystem-arm64;
  cxx = p.libcxxDylib-arm64;
  cxxabi = p.libcxxabiDylib-arm64;
  indiumSrcs = "blit-command-encoder buffer command-buffer command-encoder command-queue compute-command-encoder compute-pipeline counter depth-stencil device drawable dynamic-vk indium library memory-pool render-command-encoder render-pipeline resource sampler texture";
in pkgs.stdenv.mkDerivation {
  pname = "indium-arm64-darwin";
  version = builtins.substring 0 7 c.indiumRev;
  src = c.indiumSrc;
  patches = builtins.map (n: ./patches/indium + "/${n}") (builtins.attrNames (builtins.readDir ./patches/indium));
  nativeBuildInputs = [ pkgs.xxd ];
  dontConfigure = true;
  buildPhase = ''
    CXX=${tc}/bin/${triple}-clang++
    F="-isysroot ${sdkRoot} -mmacosx-version-min=26.5 -std=c++17 -O2 -g0 -fPIC -Qunused-arguments -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-stack-protector -I${libSystem}/usr/include -nostdinc++ -I${cxx}/usr/include/c++/v1 -Iinclude -Iprivate-include -I${pkgs.vulkan-headers}/include -I${pkgs.glm}/include"
    L="-isysroot ${sdkRoot} -mmacosx-version-min=26.5 -fuse-ld=${p.native-ld}/bin/ld -nostdlib -L${libSystem}/usr/lib -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -L${cxx}/usr/lib -L${cxxabi}/usr/lib -lc++ -lc++abi -lSystem"
    mkdir -p o lib bin gen
    for f in air dynamic-llvm iridium spirv; do
      $CXX $F -I${c.llvmArm64}/usr/include -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS \
        -DHOST_LLVM_LIBNAME='"${prefix}/lib/libLLVM.dylib"' -c src/iridium/$f.cpp -o o/iridium-$f.o &
    done
    for f in ${indiumSrcs}; do $CXX $F -c src/indium/$f.cpp -o o/indium-$f.o & done
    $CXX $F -c src/indium-kit/layer.cpp -o o/kit-layer.o &
    wait
    for f in o/*.o; do [ -s "$f" ] || { echo "missing $f"; exit 1; }; done
    $CXX -dynamiclib -install_name ${prefix}/lib/libiridium.dylib o/iridium-*.o $L -o lib/libiridium.dylib
    $CXX -dynamiclib -install_name ${prefix}/lib/libindium.dylib o/indium-*.o -Llib -liridium $L -o lib/libindium.dylib
    $CXX -dynamiclib -install_name ${prefix}/lib/libindium_kit.dylib o/kit-layer.o -Llib -lindium $L -o lib/libindium_kit.dylib
    xxd -i -n compute_add test/basic-compute/add.metallib gen/add.h
    xxd -i -n add_mslc_spv test/basic-compute-mslc/add-mslc.spv gen/add-mslc-spv.h
    xxd -i -n add_mslc_reflect test/basic-compute-mslc/add-mslc.reflect.json gen/add-mslc-reflect.h
    for t in basic-compute basic-compute-mslc device-info; do
      $CXX $F -Igen test/$t/$t.cpp -Llib -lindium_kit -lindium $L -o bin/indium-test-$t
    done
  '';
  installPhase = ''
    mkdir -p $out
    cp -a lib bin $out/
    cp LICENSE $out/LICENSE.indium
    ${tc}/bin/${triple}-otool -L $out/lib/*.dylib $out/bin/* > $out/LINKAGE.txt
  '';
  dontFixup = true;
}
