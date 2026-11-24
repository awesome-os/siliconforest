# SPDX-License-Identifier: MIT
# Installation variant also keeps all six existing System Sound wrapper APIs.
# Import the exact MIT implementation without editing the Terminal owner's file;
# its queue calls bind to our retained-PD forwarding boundary in this image.
let
  e=import ../environment.nix;
  base=import ./codecs.nix;
in base.overrideAttrs(old:{
  name="siliconforest-finch-AudioToolbox-installed-family-arm64";
  buildPhase=builtins.replaceStrings
    [ "${e.cc} -dynamiclib objects/*.o" ]
    [ "${e.cc} $CF -c ${../../terminal-app/compat/AudioToolbox.c} -o objects/SFSystemSounds.o\n${e.cc} -dynamiclib objects/*.o" ] old.buildPhase;
})
