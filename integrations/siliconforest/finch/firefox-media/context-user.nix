# SPDX-License-Identifier: MIT
let e=import ../environment.nix; base=import ../../m1-avd/build-user.nix;in
base.overrideAttrs(old:{
 name="siliconforest-sfavd-live-context-diagnostics-arm64";
 nativeBuildInputs=(old.nativeBuildInputs or [])++[e.pkgs.python3];
 buildPhase=''
  cp ${../../m1-avd/user/sfavd.c} sfavd-with-stats.c
  chmod u+w sfavd-with-stats.c
  cp ${./context-stats-trace.h} SFContextStatsTrace.h
  python3 ${./patch-context-stats.py} sfavd-with-stats.c
 ''+builtins.replaceStrings ["${../../m1-avd/user/sfavd.c}"] ["$PWD/sfavd-with-stats.c"] old.buildPhase;
})
