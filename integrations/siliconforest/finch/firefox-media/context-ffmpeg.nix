# SPDX-License-Identifier: MIT
# Same external-capture FFmpeg, only a private libsfavd metadata opt-in changes.
let e=import ../environment.nix;
 base=import ../VideoToolbox/zerocopy/ffmpeg.nix;
 originalUser=import ../../m1-avd/build-user.nix;
 tracedUser=import ./context-user.nix;
in base.overrideAttrs(old: assert
 (builtins.replaceStrings ["${originalUser}"] [""] old.configurePhase)!=old.configurePhase; {
 pname="siliconforest-ffmpeg-sfavd-live-context-diagnostics";
 configurePhase=builtins.replaceStrings ["${originalUser}"] ["${tracedUser}"] old.configurePhase;
})
