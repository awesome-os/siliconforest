# SPDX-License-Identifier: MIT
# The quiet-boot kernel (../m1-quiet-boot/kernel.nix) with ./kernel-patches on top: the boot
# need not wait for an RTC and an IOKit daemon this system does not have.
# With no further boot-args it still waits (boot bs01): the Wi-Fi driver's start is held in
# IONetworkController::start until BSD is up, so the registry cannot be quiet before the
# root mounts and the patch's registry wait runs out. Boot with sf_quiet_wait=0 sf_rtc_wait=0
# (boots bs02, bs03: root mounted 0.8 s after the kernel starts instead of 60.4 s). The
# shell then comes up while drivers still log, so the launcher must wait for a silent
# console before it types (../launcher-fast-quiet.patch).
# `--arg patched false` is ../m1-quiet-boot/kernel.nix unchanged. kernelConfig as there.
{ patched ? true, kernelConfig ? "development" }:
# Since 12 November 2026 the default source is upstream's tip (../rebase/pd.nix), where this is
# ../rebase/kernel.nix; what follows builds the pinned revision and is reached with SF_PD=pin.
if (import ../rebase/pd.nix).tip then import ../rebase/kernel.nix { inherit kernelConfig; } else
let base = import ../m1-quiet-boot/kernel.nix { inherit kernelConfig; };
in if !patched then base else base.overrideAttrs (old: {
  patches = (old.patches or [ ]) ++ [
    ./kernel-patches/0001-xnu-registry-quiet-wait-ignores-the-daemon-hold.patch
  ];
})
