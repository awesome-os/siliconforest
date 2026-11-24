# SPDX-License-Identifier: MIT
let base=import ./font-data-probe.nix;
in base.overrideAttrs(old:{
 name="sffx-font-variation-probe-arm64";
 buildCommand=builtins.replaceStrings ["${./font-data-probe.m}" "font-data-probe"] ["${./font-variation-probe.m}" "font-variation-probe"] old.buildCommand;
})
