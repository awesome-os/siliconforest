# PCIe M2: BCM4378 enumerated under Darwin on the M1 (2026-11-09)

Boot `pcie-enum-candidate-005` on the J293 under the m1n1 hypervisor; M1 returned to
Linux afterwards. Console capture: `runs/m1-desktop-010/pcie-enum-attempt-005/` (private).
Five boots were used; 001-004 found and fixed bring-up bugs (see below).

What ran:

- Launcher (`runs/m1-desktop-010/pcie-m2-host-001/lab`, `host_progress.py` + `pcie_linkup.py`):
  after `hv.init()` (which runs m1n1's `pcie_shutdown`), it does `P_PCIE_INIT`, the SMC
  `gP0d` radio-power write and the port-0 link sequence, then starts XNU with the link up.
- Kext `PDApcieT8103Bridge` (GPL-2.0, `integrations/puredarwin/m1-apcie-host/`): an
  `IOPCIBridge` on `/arm-io/apcie` using the node's own `reg` and `ranges`, plus a small
  `PDBCM4378Probe` client on the Wi-Fi function.

Kernel log, verbatim:

    PDApcieT8103: ecam phys=0x690000000 len=0x10000000 port0 phys=0x681000000 len=0x8000
    PDApcieT8103: port0 appclk=0x00100001 status=0x00000001 refclk=0x00000001 perst=0x00000001 linksts=0x99000001 intstat=0x00001000
    PDApcieT8103: root port id=0x100c106b class=0x06040001 buses=0x00010100; bus range 0-8
    PDApcieT8103: memory window pci 0xc0000000 size 0x40000000 added
    PDApcieT8103: IOPCIBridge start ok
    PDApcieT8103: after scan 1:0.0 id=0x442514e4 cmd=0x00100000 bar0=0x00000000c0c00004 bar2=0x00000000c0000004
    PDApcieT8103: after scan 1:0.1 id=0x5f6914e4 cmd=0x00100000 bar0=0x00000000c0c10004 bar2=0x00000000c0800004
    PDBCM4378Probe: 1:0.0 id=0x442514e4 class=0x02800003 bar0=0x00000000c0c00004 bar2=0x00000000c0000004
    PDBCM4378Probe: BAR0 cpu phys=0x6c0c00000 len=0x10000
    PDBCM4378Probe: window=0x18000000 chipid reg=0x10034378 chip=0x4378 rev=3 BCM4378 CONFIRMED

So under Darwin: config space works, IOPCIFamily assigns BARs, the ADT `ranges` translate
PCI 0xc0c00000 to CPU 0x6c0c00000 with no family patch, an IOPCIDevice client matches the
Wi-Fi function, and a BAR0 MMIO read returns the chip ID.

Facts learned on the way:

- m1n1's `hv_init()` calls `pcie_shutdown()`, so the launcher must bring PCIe up after it.
- On this kernel device-tree nodes are plain `IOService` objects outside the service plane
  and unregistered; the loader attaches `/arm-io/apcie` to the platform expert and starts
  the host bridge on it directly.
- `IODTResolveAddressing` returns an array owned by the registry (releasing it panics).

Not yet shown: MSI delivery, DART/DMA, firmware load, any Wi-Fi function. The link is
still brought up by the launcher, not the kext, and runs at 2.5 GT/s.

Iteration loop: `tools/pcie_iterate.sh <n>` builds the kext and KC, assembles and delivers
the packet, boots once, returns the M1 to Linux and prints the PCIe log lines (about 5 min).
