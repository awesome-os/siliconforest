# SPDX-License-Identifier: MIT
# The native-boot collection (../m1-native-boot/build-kc-norecovery.nix at the tip: the default
# collection with the platform expert that knows held cores) plus SFAppleNVMe.kext, for boots
# without the hypervisor whose root is on the internal disk. Without sfnvme= the driver does
# nothing. No default collection is changed by this file; the tip only.
#   nix-build build-kc-native.nix --no-out-link
{ kernelConfig ? "development" }:
assert (import ../rebase/pd.nix).tip;
import ../rebase/build-kc.nix {
  inherit kernelConfig;
  platform = import ../m1-native-boot/build-platform.nix;
  more = [ { name = "SFAppleNVMe"; from = import ./build-kext.nix; } ];
}
