# GPU firmware: first contact from Darwin

Boot `rk02` (10 November 2026): `PDRtkitProbe.kext` with `pdrtkitprobe=2`, GPU power domain
switched on by the launcher (`PCIE_GPU_PROBE=power`). Code: `integrations/puredarwin/m1-gpu-rtkit/`
(Linux `rtkit.c` and `mailbox.c` ported onto the compatibility layer).

What happened, from the kernel log:

1. Before: coprocessor stopped, both mailbox FIFOs empty. The five firmware memory regions from
   Apple's device tree all lie outside the memory XNU manages.
2. The probe set the RUN bit (CPU control 0 -> 0x10). The GPU firmware started.
3. RTKit handshake completed over the mailbox with interrupts (AIC 578, 4 taken): protocol
   version 12, endpoints 0x00, 0x01, 0x20, 0x21 discovered. Linux reports the same version and the
   same two application endpoints (0x20 firmware control, 0x21 doorbell) on this machine.
4. The firmware asked for a 0x2000-byte buffer. The probe refused it: giving the firmware a buffer
   needs the GPU's page tables (UAT), which the driver's `mmu.rs` sets up. Because of that the
   firmware never acknowledged the power state and `apple_rtkit_boot` returned -60 (timeout).
5. After: CPU status 0x2d (running), no crash report, Wi-Fi unaffected, clean return to Linux.

This proves on hardware: the mailbox and RTKit port, level interrupts from the coprocessor through
`PDAppleAIC`, and that the firmware iBoot loaded starts under our hypervisor boot. It is not a
working GPU: nothing was rendered, and the firmware did not reach "ready". The next step is the
driver's UAT page tables and init data, which means compiling the Rust driver itself.

Also in this session: `PDRustLKPI` phase C booted with all 21 lines as predicted (boot `rc02`:
16 KiB pages, aligned and non-blocking allocation, 20,000 lock create/drop cycles with no leak,
register read through the Rust `io` module). Extra boot arguments for one run:
`PCIE_BOOTARGS="..." tools/pcie_iterate.sh ...`.
