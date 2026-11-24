# SPDX-License-Identifier: MIT
# vk-tri for the builder (x86-64 Linux, nixpkgs' Vulkan loader) and for the M1 under Darwin (arm64
# Mach-O, linked against the loader of ./loader.nix).
#   nix-build ... test.nix -A linux    (run with lavapipe or Honeykrisp on the drm-shim: ../README.md)
#   nix-build ... test.nix -A darwin
let
  c = import ../common.nix;
  inherit (c) pkgs p triple sdkRoot;
  shaders = pkgs.runCommand "vk-tri-spirv" { nativeBuildInputs = [ pkgs.glslang ]; } ''
    mkdir -p $out
    glslangValidator -V --vn tri_vert -o $out/tri_vert.h ${./tri.vert}
    glslangValidator -V --vn tri_frag -o $out/tri_frag.h ${./tri.frag}
  '';
  loader = import ../loader.nix;
  # Mesa's copy of the kernel uAPI headers (asahi_drm.h, dma-buf.h, drm.h), MIT
  uapi = pkgs.runCommand "mesa-b7b03c5-drm-uapi" { } ''
    mkdir -p $out && tar xzf ${c.mesaSrc} --strip-components=1 -C $out --wildcards '*/include/drm-uapi/*'
  '';
  libdrm = p.libdrm-dri-arm64;
  tc = p.arm64-cross-toolchain;
in {
  inherit shaders;
  linux = pkgs.runCommandCC "vk-tri-linux" { buildInputs = [ pkgs.vulkan-headers pkgs.vulkan-loader ]; } ''
    mkdir -p $out/bin
    cc -O2 -Wall -I${shaders} ${./vk-tri.c} -lvulkan -o $out/bin/vk-tri
  '';
  darwin = pkgs.runCommand "vk-tri-arm64-darwin" { } ''
    mkdir -p $out/bin
    ${tc}/bin/${triple}-clang -O2 -Wall -isysroot ${sdkRoot} -mmacosx-version-min=26.5 \
      -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-stack-protector \
      -I${p.libsystem-arm64}/usr/include -I${pkgs.vulkan-headers}/include -I${shaders} \
      -fuse-ld=${p.native-ld}/bin/ld -nostdlib -L${p.libsystem-arm64}/usr/lib \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${p.libsystem-arm64}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -L${loader}/usr/lib \
      ${./vk-tri.c} -lvulkan -lSystem -o $out/bin/vk-tri
    ${tc}/bin/${triple}-clang -O2 -Wall -isysroot ${sdkRoot} -mmacosx-version-min=26.5 \
      -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-stack-protector \
      -I${p.libsystem-arm64}/usr/include -I${libdrm}/include -I${libdrm}/include/libdrm -I${uapi}/include \
      -fuse-ld=${p.native-ld}/bin/ld -nostdlib -L${p.libsystem-arm64}/usr/lib \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${p.libsystem-arm64}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -L${libdrm}/lib \
      ${./sync-probe.c} -ldrm -lSystem -o $out/bin/sync-probe
    ${tc}/bin/${triple}-clang -O2 -Wall -isysroot ${sdkRoot} -mmacosx-version-min=26.5 \
      -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-stack-protector \
      -I${p.libsystem-arm64}/usr/include -I${libdrm}/include -I${libdrm}/include/libdrm -I${uapi}/include \
      -fuse-ld=${p.native-ld}/bin/ld -nostdlib -L${p.libsystem-arm64}/usr/lib \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${p.libsystem-arm64}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -L${libdrm}/lib \
      ${./agx-params.c} -ldrm -lSystem -o $out/bin/agx-params
    ${tc}/bin/${triple}-clang -O2 -Wall -isysroot ${sdkRoot} -mmacosx-version-min=26.5 \
      -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-stack-protector \
      -I${p.libsystem-arm64}/usr/include -I${libdrm}/include -I${libdrm}/include/libdrm \
      -fuse-ld=${p.native-ld}/bin/ld -nostdlib -L${p.libsystem-arm64}/usr/lib \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${p.libsystem-arm64}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -L${libdrm}/lib \
      ${./wait-probe.c} -ldrm -lSystem -o $out/bin/wait-probe
  '';
}
