# SPDX-License-Identifier: MIT
# Compile the concrete I210-only SID0 IOMapper as a separate SDK variant.
# It has no personality, no CM factory call and no BME/MSI admission.
# Its presence does not qualify endpoint DMA or cable teardown on hardware.
import ./build-kext-base.nix {
  connectionManager=true;
  pciEnumeration=true;
  endpointMapper=true;
}
