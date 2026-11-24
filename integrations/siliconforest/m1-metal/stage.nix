# SPDX-License-Identifier: MIT
# The Metal experiment's files for /private/var/db/sfmetal on the M1 (tools/sfmetal.sh copies and runs
# them; no file of the installed system is replaced): Indium's libraries and tests (./indium.nix) and the
# flake's LLVM 21 libLLVM.dylib, which Iridium dlopens by the full path <prefix>/lib/libLLVM.dylib.
# Vulkan comes from the system (the loader at /usr/local/lib, Honeykrisp through its ICD file), as an
# application would get it.
{ prefix ? "/private/var/db/sfmetal", withMetal ? true }:
let
  c = import ./common.nix;
  inherit (c) pkgs p triple;
  tc = p.arm64-cross-toolchain;
  indium = import ./indium.nix { inherit prefix; };
  metal = if withMetal then import ./metal.nix { inherit prefix; } else null;
  cube = import ./indium-cube.nix { inherit prefix; };
in pkgs.runCommand "sfmetal-stage" { nativeBuildInputs = [ pkgs.python3 ]; } ''
  mkdir -p $out/lib $out/bin $out/Frameworks
  cp ${indium}/lib/*.dylib $out/lib/
  cp ${indium}/bin/* $out/bin/
  cp ${c.llvmArm64}/usr/lib/libLLVM.dylib $out/lib/libLLVM.dylib
  cp ${cube}/bin/* $out/bin/; cp ${cube}/lib/* $out/lib/
  # GLFW's Wayland backend dlopens libwayland-egl by its leaf name; the M1 has no such library
  # (the cube is run with DYLD_FALLBACK_LIBRARY_PATH=<prefix>/lib:/usr/local/lib:/usr/lib)
  cp ${p.wayland-arm64}/lib/libwayland-egl.1.dylib $out/lib/
  ${if withMetal then "cp -R ${metal}/Frameworks/. $out/Frameworks/; cp ${metal}/bin/* $out/bin/; cp ${metal}/lib/* $out/lib/" else ""}
  chmod -R u+w $out
  ${tc}/bin/${triple}-install_name_tool -id ${prefix}/lib/libLLVM.dylib $out/lib/libLLVM.dylib
  python3 ${../m1-mesa-asahi/tools/cs-rehash.py} $out/lib/libLLVM.dylib $out/lib/libglfw.3.dylib
  python3 ${../m1-mesa-asahi/tools/cs-rehash.py} --check $out/lib/*.dylib $out/bin/* ${if withMetal then "$out/Frameworks/Metal.framework/Versions/A/Metal" else ""}
  { echo "# otool -L"; for f in $out/bin/* $out/lib/*.dylib; do ${tc}/bin/${triple}-otool -L "$f"; done; } > $out/LINKAGE.txt
  cp ${indium}/LICENSE.indium $out/
''
