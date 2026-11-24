# SPDX-License-Identifier: MIT
# FXLEAD: the main Cocoa series (cocoa/patched.nix, c19 at the time of writing) plus FXLEAD's candidate fixes, for
# private Firefox A/B runs only (never installed by FXLEAD). quartzcore = the series' own QuartzCore build (TERM9
# installs its framework binary as QuartzCore.pd); quartzcoreFix = the same with the patches below.
{ patches ? [ ./quartzcore-layer-display-asks-a-view-that-updates-its-layer.patch ] }:
let
 a=import ../../cocoa/arm64.nix;
 p=import ../../cocoa/patched.nix;
 fixedSrc=a.pkgs.runCommand "fxlead-quartzcore-src" {} ''
  mkdir -p tree/src/Frameworks
  cp -r ${p.quartzcoreSrc} tree/src/Frameworks/QuartzCore
  chmod -R u+w tree
  cd tree
  ${a.pkgs.lib.concatMapStringsSep "\n" (x: "${a.pkgs.patch}/bin/patch -p1 --no-backup-if-mismatch < ${x}") patches}
  cp -r src/Frameworks/QuartzCore $out
 '';
in {
 quartzcore=p.quartzcore;
 quartzcoreFix=p.quartzcore.override { src=fixedSrc; };
}
