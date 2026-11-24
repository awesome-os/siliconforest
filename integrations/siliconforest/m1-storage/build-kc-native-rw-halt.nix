# SPDX-License-Identifier: MIT
# build-kc-rw.nix for boots without the hypervisor, with the SFCpuPower that can restart the
# machine through the SMC and reads the hardware clock (../m1-native-boot/reboot): the
# collection of a disk root that is shut down by its own command (sf-halt) and knows the time.
{ kernelConfig ? "development" }:
import ./build-kc-rw.nix { inherit kernelConfig; native = true; halt = true; }
