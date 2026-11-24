# SPDX-License-Identifier: MIT
# The default collection at the tip with SFAppleNVMeRW.kext (./build-kext-rw.nix: the
# WRITE-FENCED build of the internal NVMe driver) INSTEAD of the read-only SFAppleNVMe.kext,
# and the tree's ext4.kext replaced by ./build-ext4.nix (ext4-downgrade.patch: a downgrade to
# read-only leaves the filesystem clean on the device; `--arg ext4 false` keeps the tree's).
# Only the disk-root boots name this file; tools/default_kc.sh never does. Without sfnvme=3,
# the three partition arguments and sfnvme_write=1 nothing is written.
#   nix-build build-kc-rw.nix --no-out-link        (add --arg native true for the native-boot collection)
# Room: the write build is 7 KB larger than the read-only one and crosses a 16 KiB segment
# boundary, and the collection has about 8 KB to spare before the kernel image. Six boot kexts
# that match only on other boards' device-tree names (SG2002 SD and Ethernet, Allwinner
# display, Qualcomm I2C and pin control, the PowerVR GPU) are left out of THIS collection; the
# launcher requires them by name, so boots of this collection carry
# ./launcher-m1-members.patch (per boot, PCIE_LAB_PATCH), which removes exactly those six
# names from the required list. Every member the default collection adds for this machine is
# here. No default collection is changed. The build ends with the overlap check.
# `--arg halt true`: SFCpuPower from ../m1-native-boot/reboot/build-cpupower.nix instead of the
# default's (restart and power-off through the SMC behind a write gate, the hardware clock read:
# kern.sf_rtc_usec). ./build-kc-native-rw-halt.nix is that, natively.
{ kernelConfig ? "development", native ? false, ext4 ? true, halt ? false }:
assert (import ../rebase/pd.nix).tip;
import ../rebase/build-kc.nix ({
  inherit kernelConfig;
  omit = [ "SFSg2002Eth" "SFSg2002SD" "SFSun60iDisplay" "SFQcomGeniI2C" "SFQcomTLMM" "SFPowerVR" ]
    ++ (if ext4 then [ "ext4" ] else [ ]);
  more = [ { name = "SFAppleNVMeRW"; from = import ./build-kext-rw.nix; } ]
    ++ (if ext4 then [ { name = "ext4"; from = import ./build-ext4.nix; } ] else [ ]);
} // (if native then { platform = import ../m1-native-boot/build-platform.nix; } else { })
  // (if halt then { cpupower = import ../m1-native-boot/reboot/build-cpupower.nix; } else { }))
