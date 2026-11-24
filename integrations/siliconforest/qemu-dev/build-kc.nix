# SPDX-License-Identifier: MIT
# The kernel collection of the QEMU desktop loop: upstream's arm64 `virt` collection (RELEASE
# kernel by default) with the boot kexts rebuilt from a patched tree. The one patch makes
# /dev/usb_hid_kbd appear for QEMU's USB keyboard (see sfhid-publish-keyboard.patch).
#   [SF_PD=pin] nix-build ... build-kc.nix [--argstr kernelConfig debug] --no-out-link   ->  <out>/kernel
{ kernelConfig ? "release" }:
let
  # the SiliconForest source: ../rebase/pd.nix (the tip by default, SF_PD=pin for e7aa00a6)
  inherit (import ../rebase/pd.nix) pd pkgs;
  p = pd.packages.x86_64-linux;
  kexts = p.kexts-arm64.overrideAttrs (old: {
    src = pkgs.applyPatches {
      name = "siliconforest-qemu-dev-kexts-source";
      src = old.src;
      patches = [ ./sfhid-publish-keyboard.patch ./virtio-net-name-interface.patch ];
    };
  });
  kc = if kernelConfig == "debug" then p.kc-arm64-debug else p.kc-arm64;
in kc.override { inherit kexts; }
