# SPDX-License-Identifier: MIT
# Explicit I210 network admission variant, never selected by enum/default.
import ./build-kext-base.nix {
 connectionManager=true; pciEnumeration=true; endpointMapper=true;
 pciMSI=true; i210Network=true;
}
