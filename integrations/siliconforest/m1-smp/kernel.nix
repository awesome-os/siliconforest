# SPDX-License-Identifier: MIT
# The lab kernel (../m1-dcp/kernel.nix: the pinned T8103 bring-up kernel + its patches) with
# ./kernel-patches on top. `--arg patched false` is ../m1-dcp/kernel.nix unchanged.
# `--argstr kernelConfig development`: see ../m1-dcp/kernel.nix. DEBUG is the default.
{ patched ? true, kernelConfig ? "debug" }:
# Since 12 November 2026 the default source is upstream's tip (../rebase/pd.nix), where this is
# ../rebase/kernel.nix; what follows builds the pinned revision and is reached with SF_PD=pin.
if (import ../rebase/pd.nix).tip then import ../rebase/kernel.nix { inherit kernelConfig; } else
let base = import ../m1-dcp/kernel.nix { inherit kernelConfig; };
in if !patched then base else base.overrideAttrs (old: {
  patches = (old.patches or [ ]) ++ [
    ./kernel-patches/0001-xnu-applearmsmp-pass-reset-vector-to-iopmgr.patch
    ./kernel-patches/0002-xnu-cpu-init-midr-line-uses-kprintf.patch
    ./kernel-patches/0003-xnu-park-offline-cores-idle-accounting-deep-idle.patch
  ];
})
