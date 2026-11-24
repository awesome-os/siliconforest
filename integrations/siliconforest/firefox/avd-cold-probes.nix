# SPDX-License-Identifier: MIT
let base=import ./probes.nix;
in base.overrideAttrs(old:{
 pname="sffx-cold-avd-sandbox-probes-arm64";
 buildPhase=builtins.replaceStrings ["${./avd-sandbox-probe.c}"] ["${./avd-cold-sandbox-probe.c}"] old.buildPhase;
})
