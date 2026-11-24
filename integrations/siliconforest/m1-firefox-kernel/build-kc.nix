# SPDX-License-Identifier: MIT
# Local-only combined candidate. frozenPhase is the recorded qualified29 buildCommand.
{ frozenPhase
, hid ? import ./build-hid-readiness.nix
, spi ? import ./build-k14-touchbar.nix
, avd ? import ./build-avd-import.nix
}:
let
  source = import ../rebase/pd.nix;
  inherit (source) pkgs pd;
  original = builtins.readFile frozenPhase;
  # Explicitly retain every store root named in the frozen phase in the build closure.
  roots = pkgs.lib.unique (builtins.concatMap
    (x: if builtins.isList x then x else [])
    (builtins.split "(/nix/store/[a-z0-9]{32}-[a-zA-Z0-9+._=-]+)" original));
  exts = "System/Library/Extensions";
  strip = "${pd.packages.x86_64-linux.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-strip";
  baseline = builtins.storePath "/nix/store/xvhy0h859aq5zgkzgkl4y46gcqvwfwq9-puredarwin-kc-cio29-local-stripped";
in assert builtins.hashFile "sha256" frozenPhase == "f441db4633e65c3fe18016de21e8a883fec2dde9979f42c029fc8b46d452022a";
pkgs.runCommand "siliconforest-kc-firefox29-import-pinch-readiness" {
  nativeBuildInputs = [ pkgs.python3 pkgs.coreutils ];
  frozenInputs = map builtins.storePath roots;
} ''
  python3 ${./prepare-kc.py} ${frozenPhase} phase.sh \
    ${hid}/${exts}/IOHIDFamily.kext \
    ${spi}/${exts}/SFM1SPIHID.kext \
    ${avd}/${exts}/SFAppleAVD.kext
  python3 ${./strip-kc-inputs.py} phase.sh phase-stripped.sh "${strip}" "$PWD/stripped"
  # Compact only generated structural plist whitespace before the ORIGINAL overlap check.
  python3 - <<'PY'
from pathlib import Path
p=Path('phase-stripped.sh');s=p.read_text()
anchor='python3 /nix/store/81czk9wnmlnnkvahkqmgc95z9fvyz8bd-kc_overlap.py kernel'
assert s.count(anchor)==1
p.write_text(s.replace(anchor,'python3 ${../m1-thunderbolt/tools/compact_kc_prelink.py} kernel\n'+anchor))
PY
  . ./phase-stripped.sh
  python3 ${./verify-collection.py} ${baseline}/kernel kernel > collection-proof.json
  python3 ${./verify-cache-link.py} kernel > cache-link-proof.json
  mkdir -p "$out/nix-support"
  cp collection-proof.json cache-link-proof.json kc-strip-proof.json kc-prelink-compact-proof.json "$out/nix-support/"
''
