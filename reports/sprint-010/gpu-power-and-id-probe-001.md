# GPU: power state and identification registers before the Darwin guest starts

Boots `gp02`-`gp04` (10 November 2026). The launcher step `gpu_probe()` in
`m1-apcie-bridge/proxy_linkup.py` runs from the m1n1 proxy after `hv.init()`, before the guest.
Enable with `PCIE_GPU_PROBE=on` (reads only) or `PCIE_GPU_PROBE=power` (also turns the GPU power
domains on with m1n1's `pmgr_adt_power_enable`; the only write).

Measured:

- The GFX power domain (PMGR device 99, register 0x23b7003f8) is **off** when our guest starts:
  0x0f002300. Reads of the coprocessor registers fault while it is off.
- After power-on it reads 0x0f0023ff, and the guest then starts with the domain on.
- GPU coprocessor (`/arm-io/gfx-asc`, 0x206400000): CPU control 0 (not running), CPU status 0x2a
  (idle, stopped), both mailbox control registers 0x20001 (enabled, empty).
- Identification registers (`/arm-io/sgx` second register block, 0x204d00000 + 0x4000...):
  `0x4021100 0x40a04 0x30808 0x40404 0x1320300 0x101311`. Linux on the same machine logs exactly
  these six values ("GPU ID registers", type G13G, 8 cores).
- Core mask register (+0x1500): 0xff, eight cores. Register +0x8024 reads 0x2fffee, equal to the
  device tree's `ttbat-phys-addr-base`.
- Apple's device tree as our guest sees it has the firmware handoff: `gfx-handoff-base`
  0xbfff70000 (0x4000), `gfx-shared-region-base` 0xbfff78000 (0x40000), `gpu-region-base`
  0xbfffb8000 (0x4000), `gptbat-ready` 1, `rtkit-private-vm-region-base` 0xffffff8000000000.

Consequences for the driver port: Darwin must power GFX itself (same PMGR walk the PCIe kext
does); the coprocessor is stopped with clean mailboxes, the state the Linux driver expects at
probe; nothing has been started and no message has been sent to the GPU firmware yet.

## From Darwin (boot `gd01`)

`PDGpuDT.kext` (`integrations/puredarwin/m1-gpu-dt/`) powered GFX itself from the kernel
(0x0f002300 -> 0x0f0023ff, one register write), read the same identification registers and core
mask, and produced the Linux-style GPU properties from Apple's device tree. Its checksum over the
70 properties that can be compared with Linux is `0xb9740392`, equal to the checksum the host test
computes from this M1's Linux device tree. All 26 predicted log lines matched. Firmware 13.5,
seven operating points (0 to 1278 MHz), the three firmware memory regions as listed above.
Publishing the nodes into the registry (`pdgpudt=2`) has not been run.
