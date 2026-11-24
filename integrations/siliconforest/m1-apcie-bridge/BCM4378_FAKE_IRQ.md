# T8103 port0 fake interrupt source

This original source is a bounded **logical interrupt source in ordinary RAM**,
not a hardware MSI controller or AIC interrupt driver. It models owned vector
allocation, mask/unmask, explicit fake messages, coalesced pending bits, one-bit
acknowledgement and bounded dispatch. No real MSI message is sent. Returned
AIC numbers are arithmetic outputs, never a claimed interrupt delivery.

Exact saved J293 ADT metadata supplies message address0xfffff000, controller
MSI offset704/count32, and port0 vector base0/count8. The source accepts only
that profile and fake WLAN RID0x100; other ports and Bluetooth are excluded.
An allocation bitmap is a caller-owned model, not proof of hardware ownership.
The preserved capture was parsed offline; no new device or old survey ran.

Pinned MIT m1n1 `src/pcie.c` at121fcbf35d4501814b58ca5347926a4173b6a512 supplies
the readiness facts: PHY CTRL0 at the shared PHY, reset bit7, clock request/ack
bits0..3; PORT_STATUS0x804 RUN bit0; LINKSTS0x208 UP/BUSY/L2 bits0/2/6.
Its T602X/T6031 MSI map code is not a T8103 register programming contract.
The map does not establish T8103 MSI acknowledgement/initialization order, so
**no numeric MSI register layout is invented or programmed here**. All mask,
pending and acknowledgement fields in this source are explicitly software
model fields. No GPL source, including pcie-apple.c, is read or copied.

Every operation requires a fake PHY with reset clear and request/ack bits ready,
then RUN, link UP with BUSY/L2 clear, current PCI14e4:4425, and a final PHY check.
These reads use the existing checked fake accessor. Initial PHY refusal stops
before any port read. Link-down or reset refusal leaves the entire fake source
file and output values unchanged, with zero register writes. The late PHY check
also detects reset introduced during the sampled read sequence. Caller
serialization keeps the fake stable between that final sample and CPU commit;
this is not an atomic hardware ownership or race guarantee. A future physical
adapter still needs real PMGR/clock reachability and lifetime guarantees.

The six operations open, arm, mask, receive, dispatch and close are explicit.
Arm starts all allocated sources masked and no pending event. Masked messages
latch and repeated messages coalesce. Dispatch selects only an allocated,
unmasked pending bit, clears only that bit and returns its vector/AIC number.
No eligible source returns EMPTY without state or output changes. Close refuses
unless every allocated source is masked and no pending event remains; it does
not silently discard pending work or certify physical interrupt drain. Closed
objects cannot reopen. Counters cannot wrap. Everything is synchronous; no
handler, scheduler, thread, IRQ callback or background source exists.

Only BMI_FAKE_DEVICE with APB_HOST_TEST_ACCESSOR enables the model. KERNEL+fake
and the shared PHY/PMGR fake-write escape macro are compiler errors. Default
and KERNEL functions only return BMI_GATED, with zero MMIO, buffer changes or
GPIO152/SMC operations. There are no device/register writes even in fake mode;
logical source-file commits are ordinary CPU memory changes. The prior DART
map, chip/firmware receipts, actual hardware survey, read-only port packet and
every existing freeze remain untouched. No SDK/kext/KC or boot packet is built.

Run `python3 integrations/puredarwin/m1-apcie-bridge/test_bcm4378_fake_irq.py`.
ASan/UBSan cover all six operations,42 reset/down/clock-refusal cases, six late
reset cases,30 read-fault cases, foreign endpoint/vector/address rejection,
mask/coalescence/single-ack ordering, cleanup, counter bounds and zero register
writes. Fourteen compiled assertion-killed mutants and negative builds are
included. Leak detection is disabled for the host environment. These tests
establish software policy only, not MSI reachability, AIC delivery or Wi-Fi.
