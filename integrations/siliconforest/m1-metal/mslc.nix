# SPDX-License-Identifier: MIT
# mslc (darlinghq/mslc, MIT): a subset of the Metal Shading Language to Vulkan SPIR-V, as the library
# darling-metal's -[MTLDevice newLibraryWithSource:options:error:] loads (lib/libmslc.dylib, arm64 Darwin),
# and the same sources built on the workstation as the `mslc` command (bin/mslc-host) for checking ours.
{ prefix ? "/private/var/db/sfmetal" }:
let
  c = import ./common.nix;
  inherit (c) pkgs p triple sdkRoot;
  tc = p.arm64-cross-toolchain;
  libSystem = p.libsystem-arm64;
  cxx = p.libcxxDylib-arm64;
  cxxabi = p.libcxxabiDylib-arm64;
  srcs = "lexer preprocessor ast parser sema spirv mslc";
in pkgs.stdenv.mkDerivation {
  pname = "mslc";
  version = builtins.substring 0 7 c.mslcRev;
  src = c.mslcSrc;
  dontConfigure = true;
  buildPhase = ''
    CXX=${tc}/bin/${triple}-clang++
    F="-isysroot ${sdkRoot} -mmacosx-version-min=26.5 -std=c++17 -O2 -g0 -fPIC -Qunused-arguments -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-stack-protector -I${libSystem}/usr/include -nostdinc++ -I${cxx}/usr/include/c++/v1 -Iinclude -Isrc"
    L="-isysroot ${sdkRoot} -mmacosx-version-min=26.5 -fuse-ld=${p.native-ld}/bin/ld -nostdlib -L${libSystem}/usr/lib -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -L${cxx}/usr/lib -L${cxxabi}/usr/lib -lc++ -lc++abi -lSystem"
    mkdir -p o lib hb
    for f in ${srcs}; do $CXX $F -c src/$f.cpp -o o/$f.o & done; wait
    for f in ${srcs}; do [ -s o/$f.o ] || exit 1; done
    # 128-bit integer conversions (__floatuntidf) come from compiler-rt's builtins, linked in
    $CXX -dynamiclib -install_name ${prefix}/lib/libmslc.dylib o/*.o $L ${p.compiler-rt-arm64}/lib/darwin/libclang_rt.osx.a -o lib/libmslc.dylib
    # the command on the workstation, from the same sources
    for f in ${srcs}; do c++ -std=c++17 -O2 -Iinclude -Isrc -c src/$f.cpp -o hb/$f.o & done; wait
    c++ -std=c++17 -O2 -Iinclude -Isrc $(ls hb/*.o | grep -v hb/main.o) src/main.cpp -o mslc-host
  '';
  installPhase = ''
    mkdir -p $out/lib $out/bin $out/include
    cp lib/libmslc.dylib $out/lib/
    cp mslc-host $out/bin/
    cp -r include/. $out/include/
    cp LICENSE $out/LICENSE.mslc
  '';
  dontFixup = true;
}
