# Preparation only: retained native50 plus private lifecycle observer.
# No port/bringup/linkup/GPU/survey observer is added. Never execute a target.
let
  baseRecipe = ../../m1-trackpad-input-trace/build-kc.nix;
  bundleRecipe = ./build-private-probe-bundle-001.nix;
  base = import baseRecipe;
  probe = import bundleRecipe;
  observer = "${probe}/System/Library/Extensions/PDBCMPrivateProbe.kext";
  needle = "  -o kernel";
in
assert builtins.hashFile "sha256" baseRecipe == "6f6b694aef512f1d737459d7d44334559587d628df3814bbab73f7552bb7c9d9";
assert builtins.hashFile "sha256" bundleRecipe == "dd16c84f279cc60b64697cfdcfb17e061e2283e10b732fb4559aa2af532d7b5d";
base.overrideAttrs (old:
  assert builtins.stringLength old.buildPhase
    - builtins.stringLength (builtins.replaceStrings [ needle ] [ "" ] old.buildPhase)
    == builtins.stringLength needle;
  {
    buildPhase = ''
      echo '480e1653c6cb2bfaf6d957fbd40c6051fad67c1ce0e38224d187fd595eac0da4  ${observer}/Contents/MacOS/PDBCMPrivateProbe' | sha256sum -c -
    '' + builtins.replaceStrings [ needle ] [ ''  -kext "${observer}" \
      -o kernel'' ] old.buildPhase;
  })
