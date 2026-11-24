# SPDX-License-Identifier: MIT
# Import SDK155, NOT the current mutable media source tree, plus exact narrow delta.
let
  base = import ../m1-avd/build-kext.nix;
  inherit (import ../rebase/pd.nix) pkgs;
  frozen = builtins.storePath "/nix/store/y6pr54piv1wc2w2nkqcris680jy8yq02-puredarwin-apple-avd-tree";
in base.overrideAttrs (old: {
  pname = "siliconforest-firefox-avd-import-cache";
  src = pkgs.runCommand "siliconforest-firefox-avd-import-tree" {
    nativeBuildInputs = [ pkgs.python3 pkgs.patch ];
  } ''
    cp -R ${frozen} "$out"
    chmod -R u+w "$out"
    python3 ${./prepare-avd.py} "$out" ${./avd-source-manifest.json} ${./avd-import-cache.patch}
  '';
  installPhase = (old.installPhase or "") + ''
    mkdir -p "$out/nix-support"
    cp ${./avd-source-manifest.json} "$out/nix-support/avd-source-manifest.json"
  '';
})
