# SPDX-License-Identifier: MIT
# Rebuild the retained decoder and its CLI client against the staged public ABI.
# In particular CMBlockBufferCustomBlockSource is packed to 4 bytes (size28),
# while PD's old header declared it at default alignment (size32).
let
  e = import ../environment.nix;
  a = import ../../cocoa/arm64.nix;
  cm = import ../CoreMedia/build.nix;
  cv = import ../CoreVideo/build.nix;
  headers = import ./headers.nix;
  fw = "/System/Library/Frameworks";
  retained = import ../../m1-avd/build-videotoolbox.nix;
in retained.overrideAttrs (old: {
  name = "siliconforest-retained-videotoolbox-finch-media-arm64";
  buildPhase = builtins.replaceStrings
    [ ''CFLAGS="'' "-F${a.corevideo}${fw}" ]
    [ ''CFLAGS="-I${headers} -I${e.pd.outPath}/src/Frameworks -I${a.mesa}/usr/include -F${a.openglFramework}${fw} ''
      "-F${cm}${fw} -F${cv}${fw}" ] old.buildPhase;
  installPhase = old.installPhase + ''
    # This newly built private test-root must use the corresponding providers.
    cp ${cm}${fw}/CoreMedia.framework/Versions/A/CoreMedia $out/test-root${fw}/CoreMedia.framework/Versions/A/CoreMedia
    cp ${cv}${fw}/CoreVideo.framework/Versions/A/CoreVideo $out/test-root${fw}/CoreVideo.framework/Versions/A/CoreVideo
  '';
})
