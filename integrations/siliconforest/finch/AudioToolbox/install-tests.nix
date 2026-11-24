# SPDX-License-Identifier: MIT
let
  e=import ../environment.nix;
  base=import ./codecs-tests.nix;
  old=import ./codecs.nix;
  full=import ./install.nix;
in base.overrideAttrs(attrs:{
  name="siliconforest-finch-audio-installed-family-tests-arm64";
  buildPhase=(builtins.replaceStrings [ "${old}" ] [ "${full}" ] attrs.buildPhase)+''
    ${e.cc} $CF ${./system-sounds-probe.c} $LF -o bin/system-sounds-probe
  '';
})
