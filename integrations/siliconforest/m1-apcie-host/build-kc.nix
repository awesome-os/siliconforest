# native50 KC plus the GPL-2.0 SFApcieT8103Bridge host bridge kext.
let
  base = import ../m1-trackpad-input-trace/build-kc.nix;
  bridge = /nix/store/ivpschkfi5szkzvm78lbdq6lrqwhyrzi-puredarwin-apcie-host-0.1/System/Library/Extensions/SFApcieT8103Bridge.kext;
  needle = "  -o kernel";
in base.overrideAttrs (old: {
    buildPhase = builtins.replaceStrings [ needle ] [ ''  -kext "${bridge}" \
      -o kernel'' ] old.buildPhase;
  })
