# SPDX-License-Identifier: MIT
# Explicit first-inventory variant. Real IOCatalogue must contain only PCI
# bridges and the exact Wi-Fi personality before CM PCIe activation is allowed.
# Use with an endpoint-free collection; no DMA/MSI or hot-remove admission.
import ./build-kext-base.nix { connectionManager=true; pciEnumeration=true; }
