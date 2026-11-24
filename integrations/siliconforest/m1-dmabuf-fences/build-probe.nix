# SPDX-License-Identifier: MIT
# A standalone real-GPU dependency probe, not a kernel implementation.
let
  c = import ../m1-vulkan/common.nix;
  inherit (c) pkgs p triple sdkRoot;
  loader = import ../m1-vulkan/loader.nix;
  shader = pkgs.runCommand "siliconforest-pending-shader" { nativeBuildInputs = [ pkgs.glslang ]; } ''
    mkdir -p "$out"
    glslangValidator -V --vn pending_comp -o "$out/pending_shader.h" ${./pending.comp}
  '';
in pkgs.runCommand "siliconforest-pending-dmabuf-fence-probe" { nativeBuildInputs = [ pkgs.python3 ]; } ''
  mkdir -p "$out/bin"
  ${p.arm64-cross-toolchain}/bin/${triple}-clang -std=c11 -O1 -Wall -Wextra -Werror \
    -Wno-missing-field-initializers -Wno-misleading-indentation \
    -isysroot ${sdkRoot} -mmacosx-version-min=26.5 \
    -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-stack-protector \
    -I${p.libsystem-arm64}/usr/include -I${pkgs.vulkan-headers}/include -I${shader} \
    --ld-path=${p.native-ld}/bin/ld -nostdlib -L${p.libsystem-arm64}/usr/lib \
    -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${p.libsystem-arm64}/usr/lib/system/libdyld.dylib \
    -Wl,-platform_version,macos,26.5,26.5 -Wl,-headerpad_max_install_names -L${loader}/usr/lib \
    ${./pending-probe.c} -lvulkan -lSystem -o "$out/bin/pending-probe"
  ${p.arm64-cross-toolchain}/bin/${triple}-install_name_tool -change /usr/lib/libvulkan.1.4.341.dylib /private/var/db/sfvk/lib/libvulkan.1.dylib "$out/bin/pending-probe"
  python3 ${../m1-mesa-asahi/tools/cs-rehash.py} "$out/bin/pending-probe"
  python3 ${../m1-mesa-asahi/tools/cs-rehash.py} --check "$out/bin/pending-probe"
''
