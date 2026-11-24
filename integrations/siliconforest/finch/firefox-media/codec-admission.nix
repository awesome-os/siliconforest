# SPDX-License-Identifier: MIT
# Reuse the same public-VT/static-demux toolchain, not a second decoder provider.
let base=import ../VideoToolbox/zerocopy/capture-probe.nix; in
base.overrideAttrs(old:{
 name="siliconforest-firefox-vt-codec-admission-arm64";
 buildCommand=(builtins.replaceStrings
  ["${../VideoToolbox/zerocopy/capture-probe.py}"]
  ["${./codec-admission-port.py}"] old.buildCommand)+''
  mv $out/bin/capture-probe $out/bin/vt-codec-admission
 '';
})
