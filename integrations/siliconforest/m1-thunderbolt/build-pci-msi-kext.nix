# SPDX-License-Identifier: MIT
# Source/SDK variant only. Actual IOPCIFamily MSI indexes routed via AIC.
# No endpoint personality, BME admission, boot profile or hot-remove claim.
import ./build-kext-base.nix {
  connectionManager=true;
  pciEnumeration=true;
  endpointMapper=true;
  pciMSI=true;
}
