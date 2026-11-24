# Qualified native-only collection plus one read-only diagnostic bundle.
let
  base = import ../m1-native-input/build-kc-native-rev002.nix;
  survey = import ./build-survey-rev002.nix;
  anchor = ''      -kext "$KEXTS/PDKeyStore.kext" \
'';
  replacement = ''      -kext "$KEXTS/PDKeyStore.kext" \
      -kext "${survey}/System/Library/Extensions/PDDARTSurvey.kext" \
'';
in base.overrideAttrs (old:
  assert builtins.stringLength old.buildPhase
    - builtins.stringLength (builtins.replaceStrings [ anchor ] [ "" ] old.buildPhase)
    == builtins.stringLength anchor;
  { buildPhase = builtins.replaceStrings [ anchor ] [ replacement ] old.buildPhase; })
