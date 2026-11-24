# SPDX-License-Identifier: MIT
# Retained decoder + truthful optional encoder/pixel-session failure surface.
let
  e=import ../../environment.nix;
  base=import ../videotoolbox.nix;
in base.overrideAttrs(old:{
  name="siliconforest-retained-VideoToolbox-bottle-surface-arm64";
  nativeBuildInputs=(old.nativeBuildInputs or []) ++ [e.pkgs.python3];
  buildPhase=builtins.replaceStrings
    [ "$CC $CFLAGS -c VideoToolbox.c -o VideoToolbox.o" "$NM -g VideoToolbox.o" "exports.txt VideoToolbox.o" ]
    [ ''
        python3 ${./constants.py} ${e.finch}/userland/VideoToolbox ${./needed-constants.txt} VTMoreConstants.c
        $CC $CFLAGS -I. -c ${./vt-supplement.c} -o VTSupplement.o
        $CC $CFLAGS -c VTMoreConstants.c -o VTMoreConstants.o
        $CC $CFLAGS -c VideoToolbox.c -o VideoToolbox.o
      '' "$NM -g VideoToolbox.o VTSupplement.o VTMoreConstants.o" "exports.txt VideoToolbox.o VTSupplement.o VTMoreConstants.o" ] old.buildPhase;
})
