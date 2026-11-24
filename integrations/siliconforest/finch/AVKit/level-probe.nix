# SPDX-License-Identifier: MIT
let base=import ./reference.nix;
in base.overrideAttrs(old:{
 name="siliconforest-avkit-level-property-probe-arm64";
 buildCommand=builtins.replaceStrings ["${./reference.m}" "$out/bin/avkit-interface-reference"] ["${./level-probe.m}" "$out/bin/level-probe"] old.buildCommand;
})
