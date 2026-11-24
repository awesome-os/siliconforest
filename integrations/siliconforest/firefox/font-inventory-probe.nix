# SPDX-License-Identifier: MIT
let base=import ./font-data-probe.nix;
in base.overrideAttrs(old:{
 name="sffx-font-inventory-probe-arm64";
 buildCommand=builtins.replaceStrings ["${./font-data-probe.m}" "font-data-probe"] ["${./font-inventory-probe.m}" "font-inventory-probe"] old.buildCommand;
})
