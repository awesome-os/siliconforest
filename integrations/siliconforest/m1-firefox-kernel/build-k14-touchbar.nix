# SPDX-License-Identifier: MIT
# K14 on the installed Touch Bar SPI variant; no transport/keyboard replacement.
let
  base = import ../m1-touchbar/build-spihid.nix;
  inherit (import ../rebase/pd.nix) pkgs;
  frozen = builtins.storePath "/nix/store/fiv3mhsa74djlc6pfw6cv80160jqp755-puredarwin-m1-spi-hid-touchbar-source";
  mt = ../m1-trackpad-child/multitouch;
in base.overrideAttrs (old: {
  pname = "siliconforest-firefox-k14-touchbar";
  src = pkgs.runCommand "siliconforest-m1-spi-hid-touchbar-source" {} ''
    cp -R ${frozen} "$out"
    chmod -R u+w "$out"
    dest="$out/src/Kernel/Extensions/SFM1SPIHID"
    cp ${mt}/m1tp_mt.c ${mt}/m1tp_mt.h ${mt}/SFM1SPIHIDMultitouch.inc ${mt}/sf_tp_gesture_ring.h "$dest/"
  '';
})
