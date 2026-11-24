# Launcher changes for the PCIe/Wi-Fi boots

The boot launcher tree lives in `runs/m1-desktop-010/pcie-m2-host-001/lab` (untracked) and is
copied to the M5 by `tools/pcie_iterate.sh`. It is the read-only observer host tree
(`runs/m1-desktop-010/apcie-port-host-001/lab`, whose `host_progress.py` is preserved in
`../../m1-apcie-port-packet/host/overlay/` (under its `integrations/siliconforest/m1-bringup/` path)) plus:

- `host_progress.patch` (this directory), applied to `integrations/siliconforest/m1-bringup/host_progress.py`:
  after `hv.init()` it runs the PCIe bring-up, it accepts the two Wi-Fi kexts in the kernel
  collection, and it allows a runtime of 120-180 s.
- `../../m1-apcie-bridge/proxy_linkup.py` copied in as
  `integrations/siliconforest/m1-bringup/pcie_linkup.py` (`pcie_iterate.sh` refreshes it each run).

To rebuild the tree: copy the observer host tree, apply the patch, copy `proxy_linkup.py`.
