# SPDX-License-Identifier: MIT
# Freeze the exact deployed Wi-Fi/DART implementation; port only FXNET's PM API.
{ port ? true }:
let
  base = import ../m1-bwfm/build-kexts.nix {};
  inherit (import ../rebase/pd.nix) pkgs;
  frozen = builtins.storePath "/nix/store/a0whnzqlwxh2s66i2cl9vw2h1cc4krvd-puredarwin-bwfm-tree";
in base.overrideAttrs (old: {
  pname = if port then "siliconforest-qualified-bwfm-pm" else "siliconforest-qualified-bwfm-reproduce";
  src = frozen;
  patches = (old.patches or []) ++ pkgs.lib.optionals port [ ./fxnet-pm.patch ./pm-admission.patch ];
})
