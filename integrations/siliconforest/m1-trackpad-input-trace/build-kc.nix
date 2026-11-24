# Same native50 selection as the qualified passive child, new input-only trace bundle.
let
 base = import ../m1-native-input/build-kc-native-rev002.nix;
 next = import ./build-input-trace.nix;
 oldBundle = "/nix/store/l3kqr9zwr6pa0a3gxs9sf7bcbiarmgbj-PDM1SPIHID.kext";
 replacement = "${next}/System/Library/Extensions/SFM1SPIHID.kext";
in base.overrideAttrs (old:
 assert builtins.stringLength old.buildPhase
   - builtins.stringLength (builtins.replaceStrings [ oldBundle ] [ "" ] old.buildPhase)
   == builtins.stringLength oldBundle;
 { buildPhase = builtins.replaceStrings [ oldBundle ] [ replacement ] old.buildPhase; })
