# SPDX-License-Identifier: MIT
# Staged bounded local-file player, retaining the asset/capture family.
let
 e=import ../../environment.nix;
 a=import ../../../cocoa/arm64.nix;
 base=import ../../media/bottle/avfoundation.nix;
 vt=import ../../VideoToolbox/encoder/build.nix;
 ff=import ../../AudioToolbox/ffmpeg.nix;
 output=import ../../AudioToolbox/install.nix;
in base.overrideAttrs(old:{
 name="siliconforest-finch-AVFoundation-player-stage-arm64";
 nativeBuildInputs=(old.nativeBuildInputs or [])++[e.pkgs.python3];
 postPatch=(old.postPatch or "")+''
   python3 ${./playback.py} userland/AVFoundation/AVPlayer.m
 '';
 preBuild=(old.preBuild or "")+''
   mkdir -p playerheaders/AVFoundation
   cp ${../include}/AVFoundation/AVFoundation.h playerheaders/AVFoundation/SFAssetCore.h
 '';
 buildPhase=builtins.replaceStrings
  [ "-I${../include}" "AVAssetWriterInputGroup; do" "-lCoreGraphics -lFoundation" "-I${e.pd.outPath}/src/Frameworks" "mkdir -p objects headers/AudioToolbox" ]
  [ "-I${./include} -I$PWD/playerheaders -I${../include}" "AVAssetWriterInputGroup AVPlayer AVPlayerItemMediaDataCollector; do" "-L${a.appDeps.quartzcore}/usr/lib -lQuartzCore -lCoreGraphics -lFoundation" "-I${a.appDeps.quartzcore}/usr/include -I${a.appDeps.appkit}/usr/include -I${e.pd.outPath}/src/Frameworks" "mkdir -p objects headers/AudioToolbox playerheaders/AVFoundation; cp ${../include}/AVFoundation/AVFoundation.h playerheaders/AVFoundation/SFAssetCore.h; substituteInPlace playerheaders/AVFoundation/SFAssetCore.h --replace-fail ',*AVCustomMediaSelectionScheme;' '; @class AVCustomMediaSelectionScheme;'" ] (builtins.replaceStrings
 [ "${e.cc} -dynamiclib objects/*.o" "-lCoreGraphics -lFoundation" "-I${../include}" ]
 [ "${e.cc} $CF -I${./.} -I${../../../m1-avd/videotoolbox} -I${ff}/usr/include -c ${./SFPlayerMedia.m} -o objects/SFPlayerMedia.o; ${e.cc} -F${vt}/System/Library/Frameworks -F${output}/System/Library/Frameworks -Wl,-dylib_file,@loader_path/AudioToolbox.output-sf:${../../AudioToolbox/codec/retained-provider.tbd} -dynamiclib objects/*.o" "-F${vt}/System/Library/Frameworks -framework VideoToolbox -L${ff}/usr/lib -Wl,-hidden-lswscale -Wl,-hidden-lavutil -lCoreGraphics -lFoundation" "-I${./.} -I${../include}" ] old.buildPhase);
})
