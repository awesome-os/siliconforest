# SPDX-License-Identifier: MIT
# Private test dependency only. TERM owns canonical CoreGraphics integration.
let
 e=import ../environment.nix;
 graphics=import ../../cocoa/patched.nix;
 patched=graphics.coregraphics.overrideAttrs(old:{
  name="siliconforest-coregraphics-finite-geometry-private";
  patches=(old.patches or [])++[./patches/0005-coregraphics-finite-infinite-geometry.patch];
 });
in e.pkgs.runCommand "siliconforest-coregraphics-finite-geometry-private-framework" {} ''
 root=$out/System/Library/Frameworks/CoreGraphics.framework
 mkdir -p $root/Versions/A
 cp ${patched}/usr/lib/libCoreGraphics.dylib $root/Versions/A/CoreGraphics
 cp -rL ${patched}/usr/include/CoreGraphics $root/Versions/A/Headers
 chmod u+w $root/Versions/A/CoreGraphics
 ${e.p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-install_name_tool \
  -id /System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics $root/Versions/A/CoreGraphics
 ${e.pkgs.python3}/bin/python3 ${../../m1-mesa-asahi/tools/cs-rehash.py} $root/Versions/A/CoreGraphics
 ln -s A $root/Versions/Current
 ln -s Versions/Current/CoreGraphics $root/CoreGraphics
 ln -s Versions/Current/Headers $root/Headers
 mkdir -p $out/include
 ln -s ../System/Library/Frameworks/CoreGraphics.framework/Versions/A/Headers $out/include/CoreGraphics
 mkdir -p $out/usr/lib
 ln -s ../../System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics $out/usr/lib/libCoreGraphics.dylib
''
