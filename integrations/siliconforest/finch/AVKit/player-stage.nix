# SPDX-License-Identifier: MIT
# Coherent private prefix with real asset/capture/player/browser family and AVKit.
let e=import ../environment.nix; player=import ../AVFoundation/player/complete.nix; kit=import ./build.nix; probe=import ./player-probe.nix; values=import ./tests.nix;
in e.pkgs.runCommand "siliconforest-avkit-real-player-stage-arm64" {} ''
 mkdir -p $out/System/Library/Frameworks $out/bin
 cp -R ${player}/System/Library/Frameworks/AVFoundation.framework $out/System/Library/Frameworks/
 cp -R ${kit}/System/Library/Frameworks/AVKit.framework $out/System/Library/Frameworks/
 cp ${probe}/bin/avkit-player-probe $out/bin/
 cp ${values}/bin/avkit-test $out/bin/
 cat > $out/providers.json <<JSON
 {"player":"${player}","kit":"${kit}","probe":"${probe}","values":"${values}","installed":false}
 JSON
''
