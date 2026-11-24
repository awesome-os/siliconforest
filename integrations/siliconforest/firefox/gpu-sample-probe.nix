# SPDX-License-Identifier: MIT
let
 c=import ../m1-vulkan/common.nix;
 e=import ../finch/environment.nix;
 mesa=import ../m1-vulkan/mesa-darwin.nix;
 drm=c.p.libdrm-dri-arm64;
in c.pkgs.runCommand "sffx-gpu-cross-exec-dmabuf-sampler-probe-arm64" {} ''
 ${e.cc} -O2 -isysroot ${e.sdk} -I${e.libsystem}/usr/include \
  -I${mesa}/usr/include -I${drm}/include -I${drm}/include/libdrm \
  ${./gpu-sample-probe.c} --ld-path=${e.ld} -nostdlib -L${e.libsystem}/usr/lib \
  -L${mesa}/usr/lib -L${drm}/lib -lEGL -lGLESv2 -ldrm -lSystem \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 -o probe
 mkdir -p $out/bin;cp probe $out/bin/gpu-sample-probe
''
