# SPDX-License-Identifier: MIT
# Private paired FFmpeg/VT candidate; no installed provider changes.
# Default FFmpeg display-order behavior and the hardware callback path stay intact.
let
 e=import ../finch/environment.nix;
 original=import ../m1-avd/build-ffmpeg-sfavd.nix;
 ffmpeg=original.overrideAttrs(old:{
  pname="sffx-ffmpeg-private-decode-order";
  patches=old.patches ++ [./0004-ffmpeg-private-decode-order.patch];
 });
 base=import ../finch/VideoToolbox/encoder/build.nix;
in base.overrideAttrs(old:{
 name="sffx-videotoolbox-private-decode-order";
 patches=(old.patches or []) ++ [./0005-videotoolbox-private-decode-order.patch ./0006-vt-probe-per-packet-callback.patch];
 buildPhase=builtins.replaceStrings ["${original}"] ["${ffmpeg}"] old.buildPhase;
})
