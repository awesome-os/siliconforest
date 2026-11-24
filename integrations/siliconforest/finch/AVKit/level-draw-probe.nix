# SPDX-License-Identifier: MIT
let base=import ./player-probe.nix;
in base.overrideAttrs(old:{
 name="siliconforest-avkit-level-drawing-test-arm64";
 buildCommand=builtins.replaceStrings ["${./player-probe.m}" "$out/bin/avkit-player-probe"] ["${./level-draw-probe.m}" "$out/bin/level-draw-probe"] old.buildCommand;
})
