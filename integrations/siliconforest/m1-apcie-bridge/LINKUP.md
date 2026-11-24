# Port-0 link stage (slice 3)

`linkup.c` / `linkup.h` hold original C for the T8103 port-0 hardware steps that
the Linux Apple PCIe driver performs and that slice 2 (the m1n1 sequence) does
not have. The Linux driver was read as a hardware map only; nothing from it is
in this tree (see `NOTICE-linkup`). Board values come from the J293 ADT.

`apl_link_port0` runs once, after a completed `apb_bringup_port0`. It uses the
same `apb_io` register callbacks and adds a `apl_pins` pair for PERST# and an
optional power-enable pin. No adapter code calls it.

## Map: Linux T8103 port steps against this tree

| Hardware step | Slice 2 | Slice 3 step | Gate |
|---|---|---|---|
| Application clock on (port +0x800 bit 0) | done | 1: checked, not rewritten | read only |
| Hold the device in reset on the PERST# pin | — | 2 | pin, gated |
| Optional power-enable pin on | — | 3 (J293 ADT declares none) | pin, gated |
| Lane bank config access on (lane +0x4 bit 15) | — | 4 | shared PHY, gated |
| Lane refclk 0 request, wait for ack (bits 0/2, 50 ms) | — | 5 | shared PHY, gated |
| Lane refclk 1 request, wait for ack (bits 1/3, 50 ms) | — | 6 | shared PHY, gated |
| Lane bank config access off | — | 7 | shared PHY, gated |
| Lane refclk enable (bits 9 and 10) | — | 8 | shared PHY, gated |
| Port refclk on (port +0x810 bit 0) | — | 9 | port |
| Refclk-to-PERST wait (ADT `t-refclk-to-perst`, 100 µs) | — | 10 | delay |
| Port PERST off (+0x814 bit 0), then release the pin | register only | 11 | port, then pin gated |
| PERST-to-config wait (ADT `perst-to-config`, 100 ms) | — | 12 | delay |
| Wait for port ready (+0x804 bit 0, 250 ms) | after its own release | 13 | read only |
| Allow refclk and appclk gating (bit 8 of +0x810 and +0x800 cleared) | — | 14 | port |
| Mask all port interrupts, clear status and link command status (+0x104, +0x100, +0x210) | — | 15 | port |
| MSI doorbell (+0x168), base 0 (+0x128), enable with log2 vectors (+0x124) | — | 16 | port |
| Requester-to-stream table: probe size, clear all (+0x828, 64 slots) | — | 17 | port |
| Program requester-to-stream entries (valid, stream, requester) | — | 18 | port |
| Start link training (+0x080 bit 0) | — | 19 | port |
| Wait for link up (+0x208 bit 0) | — | 20 | read only |
| Final link and interrupt status | — | 21 | read only |

Linux steps not taken here:

- **Unmasking the link-up/down interrupts and waiting on them.** We poll
  instead, and all port interrupts stay masked. Interrupt plumbing belongs to
  a later Darwin interrupt slice.
- **Clearing a device's table entry when it goes away.** There is no device
  lifecycle yet.
- **Teardown.** Linux's teardown masks interrupts, which are already masked.
- **Other paths that do not apply to the J293 internal port:**
  - The tunnelled PCIe-C paths.
  - The T602x/T8140 variants.
  - Power cycling through the power-enable pin.
  - Sleep and resume.

## Gate

The rule is the same as in `BRINGUP.md`:

- Lane-bank writes and both pin calls exist only under
  `APB_FAKE_REGISTER_FILE`, which every kernel build refuses with `#error`.
  The lane bank is ADT `reg[8]` (0x680084000), which sits inside the shared
  PHY window. PERST# and power-enable are in shared GPIO/SMC blocks.
- The stage refuses to start without a finished slice-2 record that shows
  shared writes and no gated write. A bundle can never produce such a record,
  because slice 2 stops at its first step.
- Even with a forged record, the first write this stage attempts is the
  PERST# pin call, which is gated. A bundle run therefore stops at step 2
  with two reads and no write.

## Deliberate differences from Linux

- If the link is already up, or the application clock is off, the stage
  refuses instead of skipping ahead. It only follows slice 2.
- The application clock is checked rather than set again, so that the first
  write is the gated PERST# call.
- Table entries are programmed from the plan before training, and every entry
  is read back. The stream ID is limited to 4 bits, the width the probe value
  proves. The probe value is our own.
- The MSI base is always 0, the only value the ADT shows. The vector count
  comes from the plan: the ADT gives 8 for port 0.
- The waits use the ADT timing properties, with the PCIe minimums as floors.
- The link-up timeout comes from the plan (1–1000 ms). Linux's default is 500 ms.

## Board facts from the saved J293 ADT

These are checked by `test_linkup.py --adt`:

- `function-perst`: GPIO controller `/arm-io/gpio`, pin 152, flags 0.
- `t-refclk-to-perst` = 100 and `perst-to-config` = 100.
- `msi-address` = 0xfffff000 (high word 0).
- Port 0 `#msi-vectors` = 8, with `msi-vector-base` = 0.
- No power-enable function on `pci-bridge0` or its `wlan` child.
- `function-clkreq` (GPIO 150) is present. Neither Linux nor this stage drives it.

## Open before any live use

- A Darwin GPIO service for the PERST# function.
- Whether the BCM4378 needs a separate power rail.
- Stream IDs from the DART mapper (`mapper-apcie0-wlan`).
- An IOKit `apb_io` backend.
- The slice-2 scope review for the shared PHY and PMGR writes.

## Checks

```sh
python3 -I test_linkup.py --adt <saved guest ADT> --linux-ref <pcie-apple.c>
nix --extra-experimental-features 'nix-command flakes' build --impure --file build-linkup-gate.nix
python3 -I check_linkup_bundle.py <result>
```
