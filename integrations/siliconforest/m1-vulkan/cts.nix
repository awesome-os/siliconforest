# SPDX-License-Identifier: MIT
# dEQP-VK (Khronos VK-GL-CTS, Apache-2.0; the version and external sources nixpkgs' vulkan-cts
# pins) cross-built for arm64 Darwin with the same toolchain as ./mesa-darwin.nix: target
# `vulkan_headless` (no WSI, no GL), which on Darwin uses the CTS's Unix platform code, the loader
# opened as libvulkan.1.dylib (dyld's fallback path finds /usr/local/lib/libvulkan.1.dylib).
# Output: bin/deqp-vk, the test lists (vk-default/), the data archive (archive-dir/vulkan).
# Running it on the M1: tools/sfvk-cts.sh.
let
  c = import ./common.nix;
  inherit (c) pkgs p triple sdkRoot;
  lib = pkgs.lib;
  cts = pkgs.vulkan-cts;
  tc = p.arm64-cross-toolchain;
  libSystem = p.libsystem-arm64;
  cxx = p.libcxxDylib-arm64;
  cxxabi = p.libcxxabiDylib-arm64;
  shadercFlag = lib.findFirst (f: lib.hasInfix "FETCHCONTENT_SOURCE_DIR_SHADERC" f) "" cts.cmakeFlags;
  cflags = "-isysroot ${sdkRoot} -mmacosx-version-min=26.5 -Qunused-arguments -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-stack-protector -I${libSystem}/usr/include";
  cxxflags = "${cflags} -nostdinc++ -I${cxx}/usr/include/c++/v1";
  # the two packages of external/fetch_sources.py that nixpkgs takes from the system instead (same files)
  zlibSrc = pkgs.fetchurl { url = "https://github.com/madler/zlib/releases/download/v1.2.13/zlib-1.2.13.tar.gz";
    sha256 = "b3a24de97a8fdbc835b9833169501030b8977031bcb54b3b3ac13740f846ab30"; };
  pngSrc = pkgs.fetchurl { url = "https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.50.tar.gz";
    sha256 = "71158e53cfdf2877bc99bcab33641d78df3f48e6e0daad030afe9cb8c031aa46"; };
  ldflags = "-isysroot ${sdkRoot} -mmacosx-version-min=26.5 -fuse-ld=${p.native-ld}/bin/ld -nostdlib -L${libSystem}/usr/lib -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -L${cxx}/usr/lib -L${cxxabi}/usr/lib -lc++ -lc++abi -lSystem";
in pkgs.stdenv.mkDerivation {
  pname = "deqp-vk-arm64-darwin";
  inherit (cts) version src;
  prePatch = cts.prePatch + ''
    mkdir -p external/zlib/src external/libpng/src
    tar -xzf ${zlibSrc} -C external/zlib/src --strip-components=1
    tar -xzf ${pngSrc} -C external/libpng/src --strip-components=1
    cp external/libpng/src/scripts/pnglibconf.h.prebuilt external/libpng/src/pnglibconf.h
  '';
  # Darwin runs the CTS's Unix platform code here (vulkan_headless): it then needs the Unix
  # library-type interface of vk::Platform, and the loader by its Mach-O name
  postPatch = ''
    sed -i 's/((DE_OS == DE_OS_WIN32) || (DE_OS == DE_OS_UNIX))/((DE_OS == DE_OS_WIN32) || (DE_OS == DE_OS_UNIX) || (DE_OS == DE_OS_OSX))/' \
      framework/delibs/debase/deDefs.h
    sed -i 's/"libvulkan.so.1"/"libvulkan.1.dylib"/' framework/platform/lnx/tcuLnxVulkanPlatform.cpp
    grep -q 'DE_OS == DE_OS_OSX))' framework/delibs/debase/deDefs.h
  '';
  nativeBuildInputs = [ pkgs.cmake pkgs.ninja pkgs.python3 pkgs.pkg-config ];
  dontUseCmakeConfigure = true;
  configurePhase = ''
    runHook preConfigure
    cmake -S . -B build -G Ninja \
      -DCMAKE_SYSTEM_NAME=Darwin -DCMAKE_SYSTEM_PROCESSOR=aarch64 -DCMAKE_OSX_ARCHITECTURES=arm64 \
      -DCMAKE_OSX_SYSROOT=${sdkRoot} -DCMAKE_OSX_DEPLOYMENT_TARGET=26.5 \
      -DCMAKE_C_COMPILER=${tc}/bin/${triple}-clang -DCMAKE_CXX_COMPILER=${tc}/bin/${triple}-clang++ \
      -DCMAKE_AR=${tc}/bin/${triple}-ar -DCMAKE_RANLIB=${tc}/bin/${triple}-ranlib \
      -DCMAKE_INSTALL_NAME_TOOL=${tc}/bin/${triple}-install_name_tool \
      -DCMAKE_C_FLAGS="${cflags}" -DCMAKE_CXX_FLAGS="${cxxflags}" \
      -DCMAKE_EXE_LINKER_FLAGS="${ldflags}" -DCMAKE_SHARED_LINKER_FLAGS="${ldflags}" \
      -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER -DCMAKE_BUILD_TYPE=Release \
      -DDEQP_TARGET=vulkan_headless -DSELECTED_BUILD_TARGETS=deqp-vk \
      ${shadercFlag}
    runHook postConfigure
  '';
  buildPhase = ''
    runHook preBuild
    ninja -C build deqp-vk
    runHook postBuild
  '';
  installPhase = ''
    mkdir -p $out/bin $out/archive-dir
    cp -a build/external/vulkancts/modules/vulkan/deqp-vk $out/bin/
    cp -a build/external/vulkancts/modules/vulkan/vulkan $out/archive-dir/
    cp -a external/vulkancts/mustpass/main/vk-default $out/ 2>/dev/null || cp -a build/external/vulkancts/modules/vulkan/vk-default $out/ || true
    ${tc}/bin/${triple}-otool -L $out/bin/deqp-vk > $out/LINKAGE.txt
  '';
  dontFixup = true;
}
