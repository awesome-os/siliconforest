# SPDX-License-Identifier: MIT
# The tree's arm64 IOKit (libIOKitCF.dylib, which IOKit.framework/Versions/A/IOKit links to)
# with kIOMainPortDefault.
#   tip   upstream's IOKitLib.c defines kIOMainPortDefault itself: the tree's package, unchanged
#   pin   (SF_PD=pin) relinked with guest/iokit_main_port.c; same link line otherwise
let
  source = import ../rebase/pd.nix;
  pd = source.pd;
  p = pd.packages.x86_64-linux;
in if source.tip then p.iokit-arm64 else p.iokit-arm64.overrideAttrs (old: {
  pname = "siliconforest-iokit-homebrew";
  preBuild = ''
    ${p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-clang -c ${./guest/iokit_main_port.c} -o iokit_main_port.o
  '';
  buildPhase = builtins.replaceStrings [ "-lobjc -lFoundation" ] [ "iokit_main_port.o -lobjc -lFoundation" ] old.buildPhase;
})
