# SPDX-License-Identifier: MIT
# Pinned sources of the Metal experiment (Darling's Metal stack; NOTICE has the licences) and the
# arm64 Darwin toolchain of ../m1-vulkan (the same one Mesa, Honeykrisp and the loader are built with).
let
  v = import ../m1-vulkan/common.nix;
  gh = repo: rev: sha256: v.pkgs.fetchzip { url = "https://github.com/darlinghq/${repo}/archive/${rev}.tar.gz"; inherit sha256; };
in v // rec {
  # 0BSD: Indium (Metal-like C++ API over Vulkan 1.3) and Iridium (AIR -> SPIR-V, through LLVM's C API)
  indiumRev = "e3a1f2212bdadade0afe2e10728e7673f49d3c98";
  indiumSrc = gh "indium" indiumRev "1q6fdq0fm0gh0ic2rnppg9i8wgmyp5nkk672g34z1c9wm07x37ix";
  # MPL-2.0: Metal.framework ABI over Indium (pins indium e3a1f22 and mslc 6964bb3 as submodules)
  darlingMetalRev = "57a8a2d34685f34424b961182e45c2ff2642f37f";
  darlingMetalSrc = gh "darling-metal" darlingMetalRev "1c4kvd4vjbr0kkjj3v9b0g6r1gm520877i4vz6k5m40rr0wndvx7";
  # MIT: MSL subset -> Vulkan SPIR-V
  mslcRev = "6964bb3984e26de95bbca89b08f305a6c4ff3317";
  mslcSrc = gh "mslc" mslcRev "1s168b0zig0ak0k1k37bm57w7a6dzv08ywvg0xf12bjdxp0s3hri";
  # LLVM 21 cross-built for arm64 Darwin by the SiliconForest flake (libLLVM.dylib; Iridium dlopens it)
  llvmArm64 = v.p.llvm-arm64;
  llvmHost = v.pkgs.llvmPackages_21.libllvm;
}
