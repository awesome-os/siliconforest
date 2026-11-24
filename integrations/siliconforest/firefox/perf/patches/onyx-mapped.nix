# SPDX-License-Identifier: MIT
# FXPERF candidate: TERM's main Onyx2D (patched.nix onyx2dSrc, as installed) + 0300 font files mapped once.
let
  a = import /home/chrisk/src/darwin-lab/integrations/siliconforest/cocoa/arm64.nix;
  p = import /home/chrisk/src/darwin-lab/integrations/siliconforest/cocoa/patched.nix;
  src = a.pkgs.runCommand "fxperf-onyx2d-mapped-src" {} ''
    cp -r ${p.onyx2dSrc} $out
    chmod -R u+w $out
    cd $out
    ${a.pkgs.patch}/bin/patch --batch -p4 < ${./onyx2d-font-files-mapped-once.patch}
  '';
in (a.pd."onyx2d-arm64".override { inherit src; }).overrideAttrs (old: {
  buildPhase = builtins.replaceStrings [ "$cc $cflags -c" ] [ "$cc $cflags -O2 -fno-strict-aliasing -c" ] old.buildPhase;
})
