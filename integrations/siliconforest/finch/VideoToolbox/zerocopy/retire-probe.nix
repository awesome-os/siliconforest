# SPDX-License-Identifier: MIT
let base=import ./lease-probe.nix;in
base.overrideAttrs(old:{
 name="siliconforest-video-retired-pool-lease-probe-arm64";
 buildCommand=builtins.replaceStrings ["${./lease-probe.m}" "$out/bin/lease-probe"]
   ["${./retire-probe.m}" "$out/bin/retire-probe"] old.buildCommand;
})
