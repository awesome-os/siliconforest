# SPDX-License-Identifier: MIT
# Indium/Iridium built natively on the x86-64 Linux workstation with Indium's own CMake (LLVM 21, the
# major the M1's libLLVM has): mtl2spv, to look at what Iridium makes of a metallib before it runs on
# the M1 (tools/air_report.sh). Not used on the M1.
let
  c = import ./common.nix;
  pkgs = c.pkgs;
in pkgs.stdenv.mkDerivation {
  pname = "indium-host";
  version = builtins.substring 0 7 c.indiumRev;
  src = c.indiumSrc;
  nativeBuildInputs = [ pkgs.cmake pkgs.ninja c.llvmHost.dev ];
  buildInputs = [ pkgs.vulkan-headers pkgs.vulkan-loader c.llvmHost pkgs.glm ];
  cmakeFlags = [ "-DLLVM_DIR=${c.llvmHost.dev}/lib/cmake/llvm" ];
  ninjaFlags = [ "mtl2spv" "iridium" ];
  installPhase = ''
    mkdir -p $out/bin $out/lib
    cp mtl2spv $out/bin/
    cp libiridium.so* $out/lib/ 2>/dev/null || true
  '';
  dontFixup = true;
}
