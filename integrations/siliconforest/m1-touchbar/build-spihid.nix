# SPDX-License-Identifier: MIT
# SFM1SPIHID.kext (the built-in keyboard and trackpad, ../m1-trackpad-child/multitouch/
# build-multitouch.nix) with one addition: it tells SFAppleTouchBar about the Fn key and
# about typing (./spihid). The trackpad stream's sources are taken as they are; the patch
# and the three new files are applied to a copy of its build tree.
let
  base = import ../m1-trackpad-child/multitouch/build-multitouch.nix;
  pd = (import ../rebase/pd.nix).pd;
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
in base.overrideAttrs (old: {
  pname = "siliconforest-m1-spi-hid-touchbar";
  src = pkgs.runCommand "siliconforest-m1-spi-hid-touchbar-source" { nativeBuildInputs = [ pkgs.patch ]; } ''
    cp -R ${old.src} "$out"
    chmod -R u+w "$out"
    d="$out/src/Kernel/Extensions/SFM1SPIHID"
    test -f "$d/SFM1SPIHID.cpp"
    cp ${./spihid/SFM1SPIHIDTouchBar.inc} "$d/SFM1SPIHIDTouchBar.inc"
    cp ${./spihid/sf_hid_fn.c} "$d/sf_hid_fn.c"
    cp ${./spihid/sf_hid_fn.h} "$d/sf_hid_fn.h"
    cp ${./spihid/sf_touchbar_kbd.h} "$d/sf_touchbar_kbd.h"
    patch -d "$d" -p1 < ${./spihid/spihid-touchbar.patch}
  '';
})
