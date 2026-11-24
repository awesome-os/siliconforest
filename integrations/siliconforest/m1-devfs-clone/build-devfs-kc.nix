# SPDX-License-Identifier: MIT
{ frozenPhase }:
let
 source = import ../rebase/pd.nix;
 inherit (source) pkgs pd;
 original = builtins.readFile frozenPhase;
 roots = pkgs.lib.unique (builtins.concatMap (x: if builtins.isList x then x else [])
   (builtins.split "(/nix/store/[a-z0-9]{32}-[a-zA-Z0-9+._=-]+)" original));
 core = import ./build-kernel.nix;
 dri = import ./build-dri.nix;
 strip = "${pd.packages.x86_64-linux.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-strip";
 baseline = builtins.storePath "/nix/store/xvhy0h859aq5zgkzgkl4y46gcqvwfwq9-puredarwin-kc-cio29-local-stripped";
in assert builtins.hashFile "sha256" frozenPhase == "f441db4633e65c3fe18016de21e8a883fec2dde9979f42c029fc8b46d452022a";
pkgs.runCommand "siliconforest-kc-cio29-devfs-clone-only" {
 nativeBuildInputs = [ pkgs.python3 pkgs.coreutils ];
 frozenInputs = map builtins.storePath roots;
 SF_FIREFOX_CACHE_VERIFY = ../m1-firefox-kernel/verify-cache-link.py;
} ''
 python3 ${./prepare-devfs-kc.py} ${frozenPhase} phase.sh ${core} ${dri}/System/Library/Extensions/SFDRMDri.kext
 python3 ${../m1-firefox-kernel/strip-kc-inputs.py} phase.sh phase-stripped.sh "${strip}" "$PWD/stripped"
 python3 - <<'PY'
from pathlib import Path
p=Path('phase-stripped.sh');s=p.read_text()
a='python3 /nix/store/81czk9wnmlnnkvahkqmgc95z9fvyz8bd-kc_overlap.py kernel'
assert s.count(a)==1
p.write_text(s.replace(a,'python3 ${../m1-thunderbolt/tools/compact_kc_prelink.py} kernel\n'+a))
PY
 . ./phase-stripped.sh
 python3 ${./verify-devfs-collection.py} ${baseline}/kernel kernel > "$out/nix-support/collection-proof.json"
 python3 ${./verify-dri-link.py} kernel > "$out/nix-support/devfs-api-link-proof.json"
 cp kc-strip-proof.json kc-prelink-compact-proof.json "$out/nix-support/"
''
