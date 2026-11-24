# SPDX-License-Identifier: MIT
let e=import ../environment.nix; base=import ./tests.nix; in
base.overrideAttrs(old:{
 name="siliconforest-coreimage-extent-probe-arm64";
 buildPhase=e.pkgs.lib.replaceStrings ["${./probe.m}" "coreimage-cpu-probe"] ["${./extent-probe.m}" "coreimage-extent-probe"] old.buildPhase;
 installPhase="mkdir -p $out/bin; cp coreimage-extent-probe $out/bin/";
})
