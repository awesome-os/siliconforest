# SPDX-License-Identifier: MIT
# Experimental USB2/USB3 host collection. The daily kernel is unchanged.
import ../rebase/build-kc.nix {
  platform = import ../m1-native-boot/build-platform.nix;
  cpupower = import ../m1-native-boot/reboot/build-cpupower.nix;
  omit = [ "SFSg2002Eth" "SFSg2002SD" "SFSun60iDisplay" "SFQcomGeniI2C"
           "SFQcomTLMM" "SFPowerVR" "ext4" "RavynXHCIPort" ];
  more = [
    { name = "SFAppleNVMeRW"; from = import ../m1-storage/build-kext-rw.nix; }
    { name = "ext4"; from = import ../m1-storage/build-ext4.nix; }
    { name = "SFAppleTypeC"; from = import ../m1-typec/build-kext.nix; }
    { name = "RavynXHCIPort"; from = import ./build-host.nix; }
  ];
}
