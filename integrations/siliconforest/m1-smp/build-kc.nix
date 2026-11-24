# SPDX-License-Identifier: MIT
# The desktop collection (../m1-dcp/build-kc.nix: Wi-Fi + SFAgx + SFDcp by default here)
# with the two SMP pieces swapped in:
#   - SFArmPlatformExpert.kext from ./build-platform.nix (starts the secondary cores)
#   - the kernel from ./kernel.nix (AppleARMSMP names the reset vector)
# `--arg kernelPatched false` keeps ../m1-dcp/kernel.nix: the platform kext then only
# requires each core's reset vector register to hold a vector (true under the m1n1
# hypervisor, whose loader writes it).
# Each replacement fails the evaluation when its needle is not in the base recipe.
{ agx ? true, wifi ? true, slim ? true, kernelPatched ? true }:
# Since 12 November 2026 the default source is upstream's tip (../rebase/pd.nix), where this is
# ../rebase/build-kc.nix; what follows builds the pinned revision and is reached with SF_PD=pin.
if (import ../rebase/pd.nix).tip then import ../rebase/build-kc.nix { } else
let
  base = import ../m1-dcp/build-kc.nix { inherit agx wifi slim; };
  baseKernel = import ../m1-dcp/kernel.nix { };
  kernel = import ./kernel.nix { patched = kernelPatched; };
  platform = import ./build-platform.nix;
  platformNeedle = ''-kext "$KEXTS/SFArmPlatformExpert.kext"'';
  swap = needle: replacement: text:
    let out = builtins.replaceStrings [ needle ] [ replacement ] text;
    in assert needle == replacement || out != text; out;
in base.overrideAttrs (old: {
  buildPhase = swap "${baseKernel}" "${kernel}"
    (swap platformNeedle
      ''-kext "${platform}/System/Library/Extensions/SFArmPlatformExpert.kext"''
      old.buildPhase);
})
