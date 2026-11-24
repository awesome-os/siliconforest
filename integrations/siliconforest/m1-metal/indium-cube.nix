# SPDX-License-Identifier: MIT
# Indium's own window test `cube` (test/cube, with its shaders.metallib) for arm64 Darwin: GLFW (zlib licence,
# the SiliconForest flake's glfw-sf recipe built for arm64; its Wayland backend dlopens libwayland-client and
# libxkbcommon by their Darwin names) gives the window and the VkSurfaceKHR, IndiumKit the swapchain.
# Output: bin/indium-test-cube, lib/libglfw.3.dylib (xkbcommon linked in; the M1 has none).
{ prefix ? "/private/var/db/sfmetal" }:
let
  c = import ./common.nix;
  inherit (c) pkgs p triple sdkRoot;
  tc = p.arm64-cross-toolchain;
  libSystem = p.libsystem-arm64;
  cxx = p.libcxxDylib-arm64;
  cxxabi = p.libcxxabiDylib-arm64;
  # SiliconForest's GLFW links xkbcommon in and dlopens itself by its installed path to find those symbols:
  # that path is ours here
  rename = builtins.replaceStrings [ "/usr/local/lib/libglfw.3.dylib" ] [ "${prefix}/lib/libglfw.3.dylib" ];
  glfw = (p.glfw-sf.override {
    darwinCrossToolchain = tc;
    targetTriple = triple;
    libSystem = libSystem;
    wayland = p.wayland-arm64;
    xkbcommon = p.xkbcommon-arm64;
  }).overrideAttrs (old: { postPatch = rename (old.postPatch or ""); buildPhase = rename (old.buildPhase or ""); });
  indium = import ./indium.nix { inherit prefix; };
in pkgs.stdenv.mkDerivation {
  pname = "indium-cube-arm64-darwin";
  version = builtins.substring 0 7 c.indiumRev;
  src = c.indiumSrc;
  patches = builtins.map (n: ./patches/indium + "/${n}") (builtins.attrNames (builtins.readDir ./patches/indium));
  nativeBuildInputs = [ pkgs.xxd ];
  dontConfigure = true;
  buildPhase = ''
    CXX=${tc}/bin/${triple}-clang++
    F="-isysroot ${sdkRoot} -mmacosx-version-min=26.5 -std=c++17 -O2 -g0 -Qunused-arguments -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-stack-protector -I${libSystem}/usr/include -nostdinc++ -I${cxx}/usr/include/c++/v1 -Iinclude -Iprivate-include -I${pkgs.vulkan-headers}/include -I${pkgs.glm}/include -I${glfw}/include -DGLM_ENABLE_EXPERIMENTAL -DGLFW_INCLUDE_NONE"
    L="-isysroot ${sdkRoot} -mmacosx-version-min=26.5 -fuse-ld=${p.native-ld}/bin/ld -nostdlib -L${libSystem}/usr/lib -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 -L${cxx}/usr/lib -L${cxxabi}/usr/lib -lc++ -lc++abi -lSystem"
    mkdir -p gen bin lib
    xxd -i -n shaders test/cube/shaders.metallib gen/shaders.h
    G=$(find ${glfw} -name "libglfw*.dylib" -type f | head -1)
    cp "$G" lib/libglfw.3.dylib; chmod u+w lib/libglfw.3.dylib
    ${tc}/bin/${triple}-install_name_tool -id ${prefix}/lib/libglfw.3.dylib lib/libglfw.3.dylib
    $CXX $F -Igen test/cube/cube.cpp -L${indium}/lib -lindium_kit -lindium lib/libglfw.3.dylib $L -o bin/indium-test-cube
  '';
  installPhase = ''
    mkdir -p $out; cp -a bin lib $out/
    ${tc}/bin/${triple}-otool -L $out/bin/* $out/lib/* > $out/LINKAGE.txt
  '';
  dontFixup = true;
}
