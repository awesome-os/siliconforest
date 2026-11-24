# SPDX-License-Identifier: MIT
# Reuse the reviewed cross-exec layout/reuse control with an explicit source port.
{ geometry ? "odd" }:
let
  e=import ../environment.nix;
  base=import ../../firefox/nv12-plane-probe.nix;
  original=../../firefox/nv12-plane-probe.c;
  source=e.pkgs.runCommand "strict-nv12-2d-probe-source" {} ''
    mkdir -p $out
    cp ${original} $out/strict-nv12-probe.c
    chmod u+w $out/strict-nv12-probe.c
    ${e.pkgs.python3}/bin/python3 ${./strict-nv12-probe-port.py} $out/strict-nv12-probe.c --geometry ${e.pkgs.lib.escapeShellArg geometry}
  '';
in base.overrideAttrs(old:{
  name="siliconforest-strict-nv12-2d-probe-arm64";
  buildCommand=assert builtins.length (builtins.split
      (builtins.unsafeDiscardStringContext "${original}") old.buildCommand)==3;
    builtins.replaceStrings ["${original}"] ["${source}/strict-nv12-probe.c"] old.buildCommand + ''
      mkdir -p $out/share
      cp ${source}/probe-layout.json $out/share/
    '';
})
