# SPDX-License-Identifier: MIT
# Darling-metal remains MPL-2.0. Our policy patch to its file is MPL-2.0 too.
let
  c = import ../../m1-metal/common.nix;
  prefix = "/System/Library/Frameworks/Metal.framework/Versions/A";
  base = import ../../m1-metal/metal.nix { inherit prefix; };
  indium = import ../../m1-metal/indium.nix { inherit prefix; };
  canonical = "/System/Library/Frameworks/Metal.framework/Versions/A/Metal";
in base.overrideAttrs (old: {
  pname = "siliconforest-darling-metal-opt-in-arm64";
  patches = (old.patches or []) ++ [ ./0001-real-device-opt-in.patch ];
  buildPhase = builtins.replaceStrings
    [ "${prefix}/Frameworks/Metal.framework/Versions/A/Metal" ] [ canonical ] old.buildPhase;
  installPhase = ''
    fw=$out/System/Library/Frameworks/Metal.framework
    mkdir -p $fw/Versions/A/lib $fw/Versions/A/Resources/Licenses $out/bin
    cp -a Metal.framework/. $fw/
    cp ${indium}/lib/libindium.dylib ${indium}/lib/libiridium.dylib $fw/Versions/A/lib/
    cp ${c.llvmArm64}/usr/lib/libLLVM.dylib $fw/Versions/A/lib/
    cp ${import ../../m1-metal/mslc.nix { inherit prefix; }}/lib/libmslc.dylib $fw/Versions/A/lib/
    chmod u+w $fw/Versions/A/lib/libLLVM.dylib
    ${c.p.arm64-cross-toolchain}/bin/${c.triple}-install_name_tool -id ${prefix}/lib/libLLVM.dylib $fw/Versions/A/lib/libLLVM.dylib
    ${c.pkgs.python3}/bin/python3 ${../../m1-mesa-asahi/tools/cs-rehash.py} $fw/Versions/A/lib/libLLVM.dylib
    cp -r LICENSES $fw/Versions/A/Resources/Licenses/darling-metal
    cp ${indium}/LICENSE.indium $fw/Versions/A/Resources/Licenses/
    cp ${import ../../m1-metal/mslc.nix { inherit prefix; }}/LICENSE.mslc $fw/Versions/A/Resources/Licenses/
    cp sf-metal-tri $out/bin/
  '';
})
