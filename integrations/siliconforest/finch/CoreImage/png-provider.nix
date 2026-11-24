# SPDX-License-Identifier: MIT
# Private test dependency only. TERM owns canonical Onyx2D integration/installation.
let
 e=import ../environment.nix;
 graphics=import ../../cocoa/patched.nix;
 patched=graphics.onyx2d.overrideAttrs(old:{
  name="siliconforest-onyx-png-premultiplication-private";
  patches=(old.patches or [])++[./patches/0004-onyx-png-premultiplication.patch];
 });
in e.pkgs.runCommand "siliconforest-onyx-png-premultiplication-private-framework" {} ''
 root=$out/System/Library/Frameworks/Onyx2D.framework
 mkdir -p $root/Versions/A
 cp ${patched}/usr/lib/libOnyx2D.dylib $root/Versions/A/Onyx2D
 chmod u+w $root/Versions/A/Onyx2D
 ${e.p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-install_name_tool \
  -id /System/Library/Frameworks/Onyx2D.framework/Versions/A/Onyx2D $root/Versions/A/Onyx2D
 ${e.pkgs.python3}/bin/python3 ${../../m1-mesa-asahi/tools/cs-rehash.py} $root/Versions/A/Onyx2D
 ln -s A $root/Versions/Current
 ln -s Versions/Current/Onyx2D $root/Onyx2D
 mkdir -p $out/usr/lib
 ln -s ../../System/Library/Frameworks/Onyx2D.framework/Versions/A/Onyx2D $out/usr/lib/libOnyx2D.dylib
''
