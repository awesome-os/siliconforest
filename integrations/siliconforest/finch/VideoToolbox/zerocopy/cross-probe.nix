# SPDX-License-Identifier: MIT
# Same producer lease and toolchain as the single-process probe, separate test.
let e=import ../../environment.nix; base=import ./lease-probe.nix;
in base.overrideAttrs(old: assert e.pkgs.lib.hasInfix "lease-probe.m" old.buildCommand; {
 name="siliconforest-video-cross-process-frame-lease-probe-arm64";
 buildCommand=builtins.replaceStrings ["${./lease-probe.m}" "$out/bin/lease-probe"] ["${./cross-probe.m}" "$out/bin/cross-probe"] old.buildCommand;
})
