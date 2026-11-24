# SPDX-License-Identifier: MIT
let
 e=import ../../environment.nix;
 tests=import ./tests.nix;
 previous=import ./build.nix;
 realtime=import ./realtime.nix;
in tests.overrideAttrs (old: {
 name="siliconforest-finch-realtime-audio-tests-arm64";
 buildPhase=(builtins.replaceStrings [ "${previous}" ] [ "${realtime}" ] old.buildPhase) + ''
   ${e.cc} $CF ${./realtime-probe.m} $LF -o bin/realtime-probe
 '';
})
