# SPDX-License-Identifier: MIT
# Everything the Vulkan tests need on the M1, as one directory tree for /private/var/db/sfvk (no
# file of the installed system is replaced; ./tools/sfvk.sh copies it there and runs the tests):
#   lib/   libvulkan_asahi.dylib (Honeykrisp, ./mesa-darwin.nix), libvulkan.1.dylib (the loader,
#          ./loader.nix; also as libvulkan.dylib), libdrm.2.dylib, libwayland-client.0.dylib, and
#          the same Mesa's GL (libEGL.1, libGLESv2.2, libgallium) for comparisons
#   bin/   vulkaninfo, vk-tri (./test), vkcube and vkcubepp (Wayland)
#   icd.d/asahi_icd.json   names lib/libvulkan_asahi.dylib by its full path
#   LINKAGE.txt            otool -L of every file after relocation
# Install names point into the prefix; the ad-hoc signatures are re-hashed afterwards
# (../m1-mesa-asahi/tools/cs-rehash.py). Run with VK_ICD_FILENAMES=<prefix>/icd.d/asahi_icd.json
# and DYLD_LIBRARY_PATH=<prefix>/lib (vulkaninfo and vkcube dlopen the loader by leaf name).
{ prefix ? "/private/var/db/sfvk" }:
let
  c = import ./common.nix;
  inherit (c) pkgs p triple;
  tc = p.arm64-cross-toolchain;
  mesa = import ./mesa-darwin.nix;
  loader = import ./loader.nix;
  info = import ./vulkaninfo.nix;
  cube = import ./vkcube.nix;
  tri = (import ./test/test.nix).darwin;
in pkgs.runCommand "sfvk-stage" { nativeBuildInputs = [ pkgs.python3 ]; } ''
  INT=${tc}/bin/${triple}-install_name_tool
  OTOOL=${tc}/bin/${triple}-otool
  mkdir -p $out/lib $out/bin $out/icd.d
  cp ${mesa}/usr/lib/libvulkan_asahi.dylib ${mesa}/usr/lib/libEGL.1.dylib ${mesa}/usr/lib/libGLESv2.2.dylib \
     ${mesa}/usr/lib/libgallium-*.dylib $out/lib/
  cp ${loader}/usr/lib/libvulkan.1.*.*.dylib $out/lib/libvulkan.1.dylib
  cp ${p.libdrm-dri-arm64}/lib/libdrm.2.dylib ${p.wayland-arm64}/lib/libwayland-client.0.dylib $out/lib/
  cp ${info}/usr/bin/vulkaninfo ${tri}/bin/vk-tri ${tri}/bin/sync-probe ${tri}/bin/agx-params ${tri}/bin/wait-probe ${cube}/usr/bin/vkcube ${cube}/usr/bin/vkcubepp $out/bin/
  chmod -R u+w $out
  # every reference to one of our libraries, whatever directory it names, goes to the prefix;
  # the loader's versioned name (libvulkan.1.4.341.dylib) is the file libvulkan.1.dylib
  for f in $out/lib/*.dylib $out/bin/*; do
    case "$f" in *.dylib) "$INT" -id "${prefix}/lib/$(basename "$f")" "$f";; esac
    for old in $("$OTOOL" -L "$f" | awk 'NR>1 {print $1}'); do
      b=$(basename "$old")
      case "$b" in libvulkan.1.*.dylib) b=libvulkan.1.dylib;; esac
      [ -e "$out/lib/$b" ] && "$INT" -change "$old" "${prefix}/lib/$b" "$f"
    done
    for r in $("$OTOOL" -l "$f" | awk '/LC_RPATH/ {getline; getline; print $2}'); do
      "$INT" -delete_rpath "$r" "$f"
    done
  done
  ln -s libvulkan.1.dylib $out/lib/libvulkan.dylib
  python3 ${../m1-mesa-asahi/tools/cs-rehash.py} $out/lib/*.1.dylib $out/lib/libvulkan_asahi.dylib \
    $out/lib/libgallium-*.dylib $out/lib/libGLESv2.2.dylib $out/lib/libdrm.2.dylib \
    $out/lib/libwayland-client.0.dylib $out/bin/*
  python3 ${../m1-mesa-asahi/tools/cs-rehash.py} --check $out/lib/*.dylib $out/bin/*
  sed 's|"library_path": "[^"]*"|"library_path": "${prefix}/lib/libvulkan_asahi.dylib"|' \
    ${mesa}/usr/share/vulkan/icd.d/asahi_icd.aarch64.json > $out/icd.d/asahi_icd.json
  { echo "# otool -L after relocation"; for f in $out/bin/* $out/lib/*.dylib; do "$OTOOL" -L "$f"; done; } > $out/LINKAGE.txt
  cp ${mesa}/usr/share/vulkan/libvulkan_asahi.undefined.txt $out/
''
