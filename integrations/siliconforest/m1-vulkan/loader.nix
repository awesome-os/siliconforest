# SPDX-License-Identifier: MIT
# The Khronos Vulkan loader (libvulkan.1.dylib) for arm64 Darwin: SiliconForest's recipe
# (nix/pkgs/mesa/vulkan-loader.nix, flake output vulkan-loader-arm64), headless as the flake builds
# it, plus the Wayland surface extension (VK_KHR_wayland_surface) against the flake's libwayland.
let c = import ./common.nix;
in c.p.vulkan-loader-arm64.override { wayland = c.p.wayland-arm64; }
