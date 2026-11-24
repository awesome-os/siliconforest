# PCIe M0 + M1: port 0 link up and BCM4378 visible over ECAM (2026-11-09)

One session on the J293 from the m1n1 proxy (no hypervisor, no XNU), run by
`tools/m1_pcie_cycle.py` with `integrations/puredarwin/m1-apcie-bridge/proxy_linkup.py`.
Raw capture: `runs/m1-desktop-010/pcie-proxy-linkup-001/` (private, untracked).
The M1 returned to Linux (one external reset) and its Linux Wi-Fi still works.

Observed:

- `P_PCIE_INIT` returned 0. After it: PHY CTRL `0xf`, PHYIF RUN, port STATUS ready,
  link not up (`LINKSTS 0xa9000208`).
- `dart-apcie0` MMIO is readable once `pcie_init` has run (PARAMS/CONFIG/TCR read back).
- The Linux port sequence (PERST# on GPIO 152, lane refclk, PERST release, LTSSM start)
  did **not** bring the link up on its own.
- After the SMC `gP0d` radio-power write and the same sequence again, the link came up
  (`LINKSTS 0x99000001`, link-up interrupt status bit 12 set).
- ECAM: root port `106b:100c`; bus 1 fn 0 `14e4:4425` (BCM4378 Wi-Fi), fn 1 `14e4:5f69`
  (Bluetooth). Both have 64-bit memory BAR0 and BAR2 and capabilities PM (0x48),
  MSI (0x58), vendor (0x68), PCIe (0xac). Link trained at 2.5 GT/s x1.
- Port has 16 RID-to-SID entries.
- ADT: MSI vectors are 32 AIC interrupts starting at 704, doorbell `0xfffff000`;
  port interrupts 695/698/701.

Consequences for the next steps:

- Darwin needs the radio powered before link training: either the launcher does the
  SMC `gP0d` write from the proxy before starting the hypervisor, or the kext needs an
  SMC client. Start with the launcher.
- The DART does not need the link, only `pcie_init`.

Not yet shown: anything under XNU, BAR access, MSI delivery, DMA, firmware, Wi-Fi.
