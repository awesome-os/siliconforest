# SPDX-License-Identifier: MIT
# Device-free engine/node graph, explicit error for unwired real-time output.
let
  base = import ../build.nix;
  oldToolbox = import ../../AudioToolbox/build.nix;
  toolbox = import ../../AudioToolbox/codecs.nix;
in base.overrideAttrs (old: {
  name = "siliconforest-finch-AVFAudio-codecs-engine-arm64";
  patches = (old.patches or []) ++ [ ./0001-no-silent-device-success.patch ./0002-compressed-input-blocks.patch ];
  buildPhase = builtins.replaceStrings
    [ "-I${../include}" "${oldToolbox}" "AVAudioFile AVFConstants; do" "-framework AudioToolbox" ]
    [ "-I${./include} -I${../include}" "${toolbox}" "AVAudioFile AVFConstants AVAudioEngine; do" "-framework AudioToolbox -Wl,-dylib_file,@loader_path/AudioToolbox.output-sf:${../../AudioToolbox/codec/retained-provider.tbd}" ] old.buildPhase;
  installPhase = old.installPhase + ''
    chmod -R u+w $out/System/Library/Frameworks/AVFAudio.framework/Versions/A/Headers
    cp ${./include}/AVFAudio/* $out/System/Library/Frameworks/AVFAudio.framework/Versions/A/Headers/
  '';
})
