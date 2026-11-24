# SPDX-License-Identifier: MIT
# Opt-in bounded actual I210 descriptor/doorbell trace. No boot profile.
let
 base=import ./build-ethernet-admission-kext.nix;
 inherit (import ../m1-rust-kext/pins.nix) pkgs;
in base.overrideAttrs (old: {
 pname="siliconforest-t8103-dock-ethernet-trace";
 src=pkgs.runCommand "siliconforest-dock-ethernet-trace-tree" {nativeBuildInputs=[pkgs.python3];} ''
  cp -R ${old.src} "$out"
  chmod -R u+w "$out"
  cp ${./include/sf_i210_trace.h} "$out/src/Kernel/Extensions/SFFBSDem/dev/e1000/sf_i210_trace.h"
  cp ${./include/sf_i210_trace.h} "$out/src/Kernel/fbsdkpi/sf/sf_i210_trace.h"
  cp ${./include/sf_i210_trace.h} "$out/src/Kernel/fbsdkpi/xnu/sf_i210_trace.h"
  cp ${./include/sf_pciec_dma_inspect.h} "$out/src/Kernel/fbsdkpi/xnu/sf_pciec_dma_inspect.h"
  python3 ${./tools/prepare_ethernet_trace.py} "$out"
  python3 ${./tools/prepare_ethernet_trace_log.py} "$out"
  python3 ${./tools/prepare_ethernet_dma_inspect.py} "$out"
 '';
})
