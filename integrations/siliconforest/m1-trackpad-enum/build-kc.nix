# Replace only the native keyboard bundle in the qualified native50 producer.
let
 base = import ../m1-native-input/build-kc-native-rev002.nix;
 next = import ./build-enum.nix;
 oldBundle = "/nix/store/l3kqr9zwr6pa0a3gxs9sf7bcbiarmgbj-PDM1SPIHID.kext";
 replacement = "${next}/System/Library/Extensions/PDM1SPIHID.kext";
in base.overrideAttrs (old:
 assert builtins.stringLength old.buildPhase
   - builtins.stringLength (builtins.replaceStrings [ oldBundle ] [ "" ] old.buildPhase)
   == builtins.stringLength oldBundle;
 { buildPhase = builtins.replaceStrings [ oldBundle ] [ replacement ] old.buildPhase; })
