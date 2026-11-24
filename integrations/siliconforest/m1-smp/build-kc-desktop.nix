# SPDX-License-Identifier: MIT
# The whole desktop collection: Wi-Fi + SFAgx + SFDcp (with the backlight interface) + the
# multitouch trackpad (../m1-trackpad-child/multitouch/build-kc-multitouch.nix), with the
# two SMP pieces of ./build-kc.nix swapped in (platform kext that starts the secondary
# cores, kernel that names the reset vector) and corecrypto.kext from ./build-corecrypto.nix
# (the per-cpu generator states overran each other). One CPU unless the boot-args carry cpus=N.
# `--argstr kernelConfig development`: the kernel of that XNU configuration
# (../m1-dcp/kernel.nix); the kexts are the same builds, see ../m1-dcp/README.md.
{ agx ? true, wifi ? true, slim ? true, kernelPatched ? true, kernelConfig ? "debug" }:
# Since 12 November 2026 the default source is upstream's tip (../rebase/pd.nix), where this is
# ../rebase/build-kc.nix; what follows builds the pinned revision and is reached with SF_PD=pin.
if (import ../rebase/pd.nix).tip then import ../rebase/build-kc.nix { inherit kernelConfig; } else
let
  base = import ../m1-trackpad-child/multitouch/build-kc-multitouch.nix { inherit agx wifi slim; };
  baseKernel = import ../m1-dcp/kernel.nix { };
  kernel = import ./kernel.nix { patched = kernelPatched; inherit kernelConfig; };
  platform = import ./build-platform.nix;
  platformNeedle = ''-kext "$KEXTS/SFArmPlatformExpert.kext"'';
  corecrypto = import ./build-corecrypto.nix;
  corecryptoNeedle = ''-kext "$KEXTS/corecrypto.kext"'';
  swap = needle: replacement: text:
    let out = builtins.replaceStrings [ needle ] [ replacement ] text;
    in assert needle == replacement || out != text; out;
in base.overrideAttrs (old: {
  buildPhase = swap "${baseKernel}" "${kernel}"
    (swap platformNeedle
      ''-kext "${platform}/System/Library/Extensions/SFArmPlatformExpert.kext"''
      (swap corecryptoNeedle
        ''-kext "${corecrypto}/System/Library/Extensions/corecrypto.kext"''
        old.buildPhase));
})
