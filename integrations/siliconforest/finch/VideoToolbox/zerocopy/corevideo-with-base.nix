# SPDX-License-Identifier: MIT
# Add producer lifetime ownership to one chosen full CoreVideo provider.
# The default is the already-qualified Finch/PD display-link reconciliation.
{ provider ? import ../../CoreVideo/sf-displaylink.nix }:
provider.overrideAttrs(old:{
 name="siliconforest-corevideo-frame-lifetime-stage-arm64";
 postPatch=(old.postPatch or "")+''
  python3 ${./corevideo-port.py} userland/CoreVideo/CVPixelBuffer.c
 '';
})
