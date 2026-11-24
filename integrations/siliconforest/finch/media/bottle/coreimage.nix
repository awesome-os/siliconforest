# SPDX-License-Identifier: MIT
# Canonical facade over the real retained CI classes: no second class definitions.
let
  e=import ../../environment.nix;
  base=import ../../CoreImage/build.nix;
in base.overrideAttrs(old:{
  name="siliconforest-retained-CoreImage-bottle-facade-arm64";
  patches=[];
  buildPhase=builtins.replaceStrings
    [ "CIConstants CIColor CIImage CIContext CIFilter CIFilterTable CIMisc" "-lCoreGraphics -lFoundation" ]
    [ "CIConstants" "-Wl,-reexport_library,${./retained-quartzcore.tbd} -lCoreGraphics -lFoundation" ] old.buildPhase;
})
