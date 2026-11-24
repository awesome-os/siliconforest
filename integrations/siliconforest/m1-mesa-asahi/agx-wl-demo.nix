# agx-wl-demo for the SiliconForest M1 guest: a GPU-rendered Wayland client (arm64 Mach-O) with the
# Wayland-enabled Mesa, libdrm and libwayland it runs on, in a prefix of its own so that it can be
# staged next to the bundle of agx-offscreen-test.nix (whose libEGL has no Wayland platform):
#
#   usr/local/bin/agx-wl-demo
#   usr/local/lib/mesa-asahi-wl/{libEGL.1,libGLESv2.2,libgallium-26.3.0-devel,libdrm.2,
#                                libwayland-client.0,libwayland-egl.1}.dylib
#
#   nix-build --extra-experimental-features nix-command --extra-experimental-features flakes agx-wl-demo.nix
let
  c = import ./common.nix;
  inherit (c) pkgs p triple sdkRoot;
  mesa = import ./mesa-darwin-wayland.nix;
  libdrm = p.libdrm-dri-arm64;
  wl = p.wayland-arm64;
  libSystem = p.libsystem-arm64;
  tc = p.arm64-cross-toolchain;
  prefix = "/usr/local/lib/mesa-asahi-wl";
  xdgShell = "${p.wayland-protocols}/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml";
in pkgs.stdenv.mkDerivation {
  name = "darwin-lab-agx-wl-demo-arm64-darwin";
  dontUnpack = true;
  nativeBuildInputs = [ pkgs.wayland-scanner ];
  buildPhase = ''
    wayland-scanner client-header ${xdgShell} xdg-shell-client-protocol.h
    wayland-scanner private-code ${xdgShell} xdg-shell-protocol.c
    cp ${./test/agx-panel-demo.c} agx-panel-demo.c
    cp ${./test/agx-wl-demo.c} agx-wl-demo.c
    ${tc}/bin/${triple}-clang -std=gnu11 -O2 -Wall -Wextra -Wno-unused-function -Wno-unused-variable \
      -isysroot ${sdkRoot} -mmacosx-version-min=26.5 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
      -fno-stack-protector -I${libSystem}/usr/include -I. \
      -I${mesa}/usr/include -I${libdrm}/include -I${libdrm}/include/libdrm -I${wl}/include \
      agx-wl-demo.c xdg-shell-protocol.c \
      -fuse-ld=${p.native-ld}/bin/ld -nostdlib -L${libSystem}/usr/lib -L${mesa}/usr/lib -L${libdrm}/lib -L${wl}/lib \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 -Wl,-fixup_chains \
      -lEGL -lGLESv2 -ldrm -lwayland-client -lwayland-egl -lSystem -o agx-wl-demo
  '';
  installPhase = ''
    INT=${tc}/bin/${triple}-install_name_tool
    OTOOL=${tc}/bin/${triple}-otool
    lib=$out${prefix}
    mkdir -p $out/usr/local/bin $lib
    cp agx-wl-demo $out/usr/local/bin/
    cp ${mesa}/usr/lib/libEGL.1.dylib ${mesa}/usr/lib/libGLESv2.2.dylib ${mesa}/usr/lib/libgallium-*.dylib \
       ${libdrm}/lib/libdrm.2.dylib ${wl}/lib/libwayland-client.0.dylib ${wl}/lib/libwayland-egl.1.dylib $lib/
    chmod u+w $lib/* $out/usr/local/bin/*
    for f in $lib/*.dylib $out/usr/local/bin/*; do
      case "$f" in *.dylib) "$INT" -id "${prefix}/$(basename "$f")" "$f";; esac
      for d in $lib/*.dylib; do
        b=$(basename "$d")
        for old in $("$OTOOL" -L "$f" | awk 'NR>1 {print $1}' | grep "/$b\$" || true); do
          "$INT" -change "$old" "${prefix}/$b" "$f"
        done
      done
    done
    # install_name_tool edited the first page of every file after the linker signed it
    ${pkgs.python3}/bin/python3 ${./tools/cs-rehash.py} $out/usr/local/bin/* $lib/*.dylib
    ${pkgs.python3}/bin/python3 ${./tools/cs-rehash.py} --check $out/usr/local/bin/* $lib/*.dylib
    { echo "# otool -L after relocation"; for f in $out/usr/local/bin/* $lib/*.dylib; do "$OTOOL" -L "$f"; done; } > $out/LINKAGE.txt
    if grep -n '@rpath\|/nix/store/.*dylib (' $out/LINKAGE.txt | grep -v '^[0-9]*:/nix/store/[^ ]*:$'; then
      echo "unrelocated library reference" >&2; exit 1
    fi
  '';
  dontFixup = true;
  dontStrip = true;
}
