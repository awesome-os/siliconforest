# SPDX-License-Identifier: MIT
# The default desktop kernel (../m1-boot-speed/kernel.nix) with ./kernel-patches on top: the
# kernel never takes Apple's "set recovery variables in NVRAM and restart" path.
{ patched ? true, kernelConfig ? "development" }:
# Since 12 November 2026 the default source is upstream's tip (../rebase/pd.nix), where this is
# ../rebase/kernel.nix; what follows builds the pinned revision and is reached with SF_PD=pin.
if (import ../rebase/pd.nix).tip then import ../rebase/kernel.nix { inherit kernelConfig; } else
let base = import ../m1-boot-speed/kernel.nix { inherit kernelConfig; };
in if !patched then base else base.overrideAttrs (old: {
  patches = (old.patches or [ ]) ++ [
    ./kernel-patches/0001-xnu-IOSetRecoveryBoot-does-nothing.patch
  ];
})
