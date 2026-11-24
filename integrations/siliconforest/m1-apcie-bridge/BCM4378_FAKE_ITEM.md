# Opaque host ring item, revision 001

This is original host-only fixture code. It does **not** define or implement a
Broadcom firmware msgbuf ABI. The locally pinned brcmfmac directory contains
only `pcie.c`, `firmware.c` and `chip.c`; no msgbuf source/header was acquired.
No sources or firmware were fetched for this slice.

The fallback is the unchanged ring receipt
`reports/sprint-010/bcm4378-fake-ring-source-001.json` and its hash-bound C test.
That test submits one eight-byte opaque item: `11 22 33 44 55 66 77 88`.
The new code gives those bytes no message type, opcode, flags, request identity,
field endian or firmware meaning. Do not send this fixture to physical firmware.
Actual message definitions and their length/ownership rules remain missing.

`bfi_encode` writes exactly those eight bytes to independent caller storage;
`bfi_check` requires exactly eight bytes and checks every byte. Short, oversized,
null and altered items are rejected. Failed encoding preserves output. Encoding
cannot overwrite the owned ring buffer or either owner object. Checking a
retained ring slot is read-only and allowed. `bfi_submit` validates first, then
uses the unchanged fake `bcr_submit` for one preallocated host slot, index update
and BCM endpoint doorbell. The caller serializes all owners and keeps their
storage/accessor alive. No allocation, firmware bytes or DMA is involved.

Every new operation checks the existing fake accessor's PHY reset/clocks,
port RUN/link/not-BUSY/not-L2 and selected fake PCI identity, followed by a PHY
recheck. Link down or reset refuses before any item-output, ring-buffer or
register write. Once submission enters the old ring, its per-access guards,
partial-write fault latch, retained ownership and no-retry rules apply. A guard
read failure before ring admission leaves the ring unchanged and is returned as
an error; it is not a successful submission or a physical state observation.

The working build requires all of `BFI_FAKE_DEVICE`, `BCR_FAKE_DEVICE`,
`BCT_FAKE_BAR_FILE` and `APB_HOST_TEST_ACCESSOR`. Default and host-compiled
`KERNEL` builds return `BFI_GATED` from all three operations without I/O or output
changes. Kernel+fake, missing fake prerequisites and shared PHY/PMGR writer
opt-ins are compile-refused. GPIO 152 and the SMC radio command remain absent.
This code adds no T8103 MSI register layout.

Run `python3 integrations/puredarwin/m1-apcie-bridge/test_bcm4378_fake_item.py`.
The ASan/UBSan suite uses the exact previous fake BAR backend without running
its old main. It checks exact bytes/length, all single-bit corruption, initial
and late readiness failures, full-ring refusal and every submission callback
failure under both failed-write application models. Mutation tests count only
compiled programs that fail assertions. Default/kernel and negative-build tests
establish software gates, not SDK or hardware readiness.

The actual saved PCIe observation is still PHY_NOT_READY. This work makes no
claim about a real chip, BAR, DMA, firmware load or usable Wi-Fi. No device was
opened, no real memory mapped, no kext/KC linked and no boot packet prepared.
