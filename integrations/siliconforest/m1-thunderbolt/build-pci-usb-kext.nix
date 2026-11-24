# SPDX-License-Identifier: MIT
# Explicit dock USB matching/admission SDK; never selected by default.
import ./build-kext-base.nix {
 connectionManager=true; pciEnumeration=true; endpointMapper=true;
 pciMSI=true; i210Network=true; dockUSB=true;
}
