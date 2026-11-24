# SPDX-License-Identifier: MIT
# Mesa b7b03c5 (./common.nix) for the M1 under Darwin: Honeykrisp (libvulkan_asahi.dylib, Vulkan
# runtime, Wayland WSI) beside the gallium asahi GL driver, EGL on Wayland/surfaceless, GLES.
# The recipe is ../m1-mesa-asahi/mesa-darwin-build.nix with that Mesa's ten patches (they apply
# to b7b03c5 unchanged) and the Vulkan patches of ./patches.
#   nix-build ... mesa-darwin.nix
let
  c = import ./common.nix;
  patches = builtins.filter (p: builtins.match ".*\\.patch" (toString p) != null)
    (map (n: ./patches + "/${n}") (builtins.attrNames (builtins.readDir ./patches)));
in import ../m1-mesa-asahi/mesa-darwin-build.nix {
  wayland = true;
  vulkan = true;
  inherit c;
  nativeTools = import ./mesa-native.nix;
  extraPatches = patches;
}
