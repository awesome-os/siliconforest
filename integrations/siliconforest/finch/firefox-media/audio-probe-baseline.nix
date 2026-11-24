# SPDX-License-Identifier: MIT
# Same query/lifecycle source without the diagnostic interposer: isolate loader
# failures from device/service behavior. It does not install a provider.
let base=import ./audio-probe.nix;observer=import ./hal-observer.nix; in
base.overrideAttrs(old:{
 name="siliconforest-firefox-media-audio-probe-baseline-arm64";
 buildCommand=builtins.replaceStrings [
  "cp ${observer}/lib/SFFirefoxMediaObserver.dylib $out/lib/"
  "-Wl,-needed_library,${observer}/lib/SFFirefoxMediaObserver.dylib"
 ] ["" ""] old.buildCommand;
})
