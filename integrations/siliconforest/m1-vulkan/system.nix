# SPDX-License-Identifier: MIT
# Graphics of the default install, as a tree rooted at / (tools/sf-gpu-install.sh puts it on the
# M1; the userland installer's part `gpu`): Omacom's Mesa b7b03c5 (./mesa-darwin.nix: GL with the
# clock and ETIME fixes, and Honeykrisp), the Khronos loader, vulkaninfo and vkcube, at the paths
# a program finds with no environment variable set:
#   /usr/local/lib/mesa-asahi/   libEGL.1, libGLESv2.2, libgallium-26.3.0-devel (the names the
#                                installed GL Mesa 147a62f has; the compositor links them there),
#                                libvulkan_asahi (Honeykrisp), libwayland-client.0 (this GL is built
#                                with the Wayland EGL platform; 147a62f was not). libdrm.2 stays the
#                                installed one (same 212 exports; that build links libIOKitCF)
#   /usr/local/lib/libvulkan.1.dylib (+ libvulkan.dylib)   vulkaninfo and vkcube dlopen the loader
#                                by leaf name: dyld's fallback path has /usr/local/lib
#   /etc/vulkan/icd.d/asahi_icd.json   the loader's system directory (EXTRASYSCONFDIR /etc, and
#                                XDG_CONFIG_DIRS=/etc as /etc/sf-environment sets it; XDG_DATA_DIRS there is
#                                /usr/share:/share, so /usr/local/share is not searched)
#   /usr/local/bin/              vulkaninfo, vkcube, vkcubepp
# Install names point at those paths; ad-hoc signatures re-hashed afterwards, as ./stage.nix does.
let
  c = import ./common.nix;
  inherit (c) pkgs p triple;
  tc = p.arm64-cross-toolchain;
  mesa = import ./mesa-darwin.nix;
  loader = import ./loader.nix;
  info = import ./vulkaninfo.nix;
  cube = import ./vkcube.nix;
  MA = "/usr/local/lib/mesa-asahi";
in pkgs.runCommand "sf-gpu-system" { nativeBuildInputs = [ pkgs.python3 ]; } ''
  INT=${tc}/bin/${triple}-install_name_tool
  OTOOL=${tc}/bin/${triple}-otool
  R=$out/root; L=$R${MA}
  mkdir -p $L $R/usr/local/bin $R/etc/vulkan/icd.d
  cp ${mesa}/usr/lib/libvulkan_asahi.dylib ${mesa}/usr/lib/libEGL.1.dylib ${mesa}/usr/lib/libGLESv2.2.dylib \
     ${mesa}/usr/lib/libgallium-*.dylib $L/
  cp ${p.libdrm-dri-arm64}/lib/libdrm.2.dylib ${p.wayland-arm64}/lib/libwayland-client.0.dylib $L/
  cp ${loader}/usr/lib/libvulkan.1.*.*.dylib $R/usr/local/lib/libvulkan.1.dylib
  cp ${info}/usr/bin/vulkaninfo ${cube}/usr/bin/vkcube ${cube}/usr/bin/vkcubepp $R/usr/local/bin/
  chmod -R u+w $R
  where() { case "$1" in libvulkan.1.dylib) echo /usr/local/lib/$1;; *) echo ${MA}/$1;; esac; }
  for f in $L/*.dylib $R/usr/local/lib/libvulkan.1.dylib $R/usr/local/bin/*; do
    case "$f" in *.dylib) "$INT" -id "$(where "$(basename "$f")")" "$f";; esac
    for old in $("$OTOOL" -L "$f" | awk 'NR>1 {print $1}'); do
      b=$(basename "$old")
      case "$b" in libvulkan.1.*.dylib) b=libvulkan.1.dylib;; esac
      if [ -e "$L/$b" ] || [ "$b" = libvulkan.1.dylib ]; then "$INT" -change "$old" "$(where "$b")" "$f"; fi
    done
    for r in $("$OTOOL" -l "$f" | awk '/LC_RPATH/ {getline; getline; print $2}'); do
      "$INT" -delete_rpath "$r" "$f"
    done
  done
  ln -s libvulkan.1.dylib $R/usr/local/lib/libvulkan.dylib
  python3 ${../m1-mesa-asahi/tools/cs-rehash.py} $L/*.dylib $R/usr/local/lib/libvulkan.1.dylib $R/usr/local/bin/*
  python3 ${../m1-mesa-asahi/tools/cs-rehash.py} --check $L/*.dylib $R/usr/local/lib/libvulkan.1.dylib $R/usr/local/bin/*
  rm $L/libdrm.2.dylib   # only needed above, to point every reference at ${MA}/libdrm.2.dylib
  sed 's|"library_path": "[^"]*"|"library_path": "${MA}/libvulkan_asahi.dylib"|' \
    ${mesa}/usr/share/vulkan/icd.d/asahi_icd.aarch64.json > $R/etc/vulkan/icd.d/asahi_icd.json
  { echo "# otool -L after relocation"; for f in $R/usr/local/bin/* $R/usr/local/lib/libvulkan.1.dylib $L/*.dylib; do "$OTOOL" -L "$f"; done; } > $out/LINKAGE.txt
  (cd $R && find . \( -type f -o -type l \) | LC_ALL=C sort) > $out/FILES.txt
''
