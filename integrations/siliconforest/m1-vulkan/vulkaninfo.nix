# SPDX-License-Identifier: MIT
# vulkaninfo for arm64 Darwin: SiliconForest's vulkan-tools recipe (flake output vulkan-tools-arm64)
# without X11 (then only vulkaninfo is built), linked against ./loader.nix.
let c = import ./common.nix;
in c.p.vulkan-tools-arm64.override { withX11 = false; vulkanLoader = import ./loader.nix; }
