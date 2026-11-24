# SPDX-License-Identifier: MIT
# vkcube (Khronos Vulkan-Tools, the version of the flake's nixpkgs) for arm64 Darwin on Wayland:
# ./vulkaninfo.nix's recipe (SiliconForest's nix/pkgs/mesa/vulkan-tools.nix, no X11) with the cube
# built for VK_KHR_wayland_surface (xdg-shell, xdg-decoration) against the flake's libwayland.
# The cube dlopens libwayland-client by its Linux soname; here it is libwayland-client.0.dylib.
let
  c = import ./common.nix;
  inherit (c) pkgs p;
  lib = pkgs.lib;
  wl = p.wayland-arm64;
  ffi = p.libffi-arm64;
  wlPc = lib.concatStringsSep ":" [
    "${wl}/lib/pkgconfig" "${p.wayland-protocols}/share/pkgconfig"
    "${lib.getDev pkgs.wayland-scanner}/lib/pkgconfig" "${ffi}/lib/pkgconfig" "${ffi}/usr/lib/pkgconfig" ];
  base = import ./vulkaninfo.nix;
in base.overrideAttrs (old: {
  pname = "sf-vkcube-wayland";
  nativeBuildInputs = old.nativeBuildInputs ++ [ pkgs.wayland-scanner ];
  postPatch = old.postPatch + ''
    sed -i -e 's/"libwayland-client\.so\.0"/"libwayland-client.0.dylib"/' \
           -e 's/"libwayland-client\.so"/"libwayland-client.dylib"/' cube/wayland_loader.h
    # cube.c takes the evdev codes of its Wayland keys from <linux/input.h> (values of Linux's
    # input-event-codes.h, which is what the compositor sends in wl_keyboard.key)
    mkdir -p compat/linux
    printf '%s\n' '#define KEY_ESC 1' '#define KEY_SPACE 57' '#define KEY_LEFT 105' \
      '#define KEY_RIGHT 106' '#define KEY_DOWN 108' '#define BTN_LEFT 0x110' > compat/linux/input.h
    # SFVK_CUBE_INSTANCES=<n>: the cube drawn n times on top of itself each frame (depth LESS_OR_EQUAL, so
    # every instance is shaded; the picture is the same): a heavy frame per present, for the
    # present-race test (tools/sfvk-present-race.sh). Unset: one draw, as upstream.
    sed -i '/^#include <stdlib.h>$/a static uint32_t sf_cube_instances(void) { const char *e = getenv("SFVK_CUBE_INSTANCES"); long n = e ? strtol(e, 0, 10) : 1; return n > 0 ? (uint32_t)n : 1; }' cube/cube.c
    substituteInPlace cube/cube.c --replace-fail 'vkCmdDraw(submission_resource->cmd, 12 * 3, 1, 0, 0);' \
      'vkCmdDraw(submission_resource->cmd, 12 * 3, sf_cube_instances(), 0, 0);'
    # the Darwin libwayland has no share/wayland; the protocol is the same file in the native one
    substituteInPlace cube/CMakeLists.txt --replace-fail '$'{WAYLAND_CLIENT_PATH}/wayland.xml \
      ${pkgs.wayland-scanner}/share/wayland/wayland.xml
  '';
  configurePhase = builtins.replaceStrings
    [ "export PKG_CONFIG_LIBDIR=\"$PKG_CONFIG_PATH\""
      "-DBUILD_CUBE=OFF"
      "-DBUILD_WSI_WAYLAND_SUPPORT=OFF"
      "-I${p.libsystem-arm64}/usr/include" ]
    [ "export PKG_CONFIG_PATH=\"${wlPc}\"\n    export PKG_CONFIG_LIBDIR=\"$PKG_CONFIG_PATH\""
      "-DBUILD_CUBE=ON -DCUBE_WSI_SELECTION=WAYLAND -DBUILD_VULKANINFO=OFF"
      "-DBUILD_WSI_WAYLAND_SUPPORT=ON"
      "-I${p.libsystem-arm64}/usr/include -I${wl}/include -I$PWD/compat" ]
    old.configurePhase;
})
