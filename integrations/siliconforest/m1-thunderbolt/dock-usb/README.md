# Dedicated PCI dock xHCI preparation

Source only: no boot collection selects this driver. The native ARM64 SDK
build passes, but no USB controller, keyboard, storage or hot-unplug result
has been observed on the M1.

`build-dock-usb-kext.nix` prepares a separate FL1100-only variant of the
existing Ravyn xHCI lifetime/read-only-storage port. It requires actual host
access binding and shared PCIe-C IOMapper lookup. It maps native BAR resources
with posted writes, has no BAR sizing/address fallback, and requests checked
late BME only after real MSI registration and arming. Scalar registers,
doorbells, extended capabilities and BIOS handoff use guarded accessors.
Physical loss, bounds refusal or all-ones reads latch failure, close operation
admission and stop workers; they cannot qualify a successful halt/reset.

All13 ring/payload allocation sites use whole16KiB owned backing and actual
IODMACommand translations. A controller ledger retains every allocation
until operation/worker draining and real whole-controller halt/reset, then
explicitly completes/clears each mapping. Ordinary ring/payload release sites
drop client references while that ledger preserves backing. Failed prepare,
complete or clear keeps backing until reboot. A partial retirement failure
keeps the entire ledger/context and never reports a successful drain.

This initial variant does not reclaim mappings while the controller runs.
Its ledger is bounded4096 entries; the shared mapper currently limits active
mappings to2048 and IOVA space to256MiB. Exhaustion refuses allocation. This
is suitable for initial qualification, not an unlimited storage workload.
BOT cache reuse uses explicit failed-transfer state, because command/mapper
retains make a native buffer's reference count unsuitable for that decision.
Client pages never bypass the owned bounces. Storage writes are rejected by
the separate read-only qualification patch.

The detached factory identifies the Keychron branch's actual FL1100 through
Alpine Ridge port0 ancestry, registers its real SID0 requester without
resetting the active I210 root/allocator, binds the native mapper/resources,
allocates MSI, and binds retained host access. The host's late USB admission
preserves actual I210 grants and admits only the selected USB ancestry after
mapper/MSI/physical checks. The separate build-pci-usb-kext.nix profile accepts exactly this FL1100
personality and exposes one administrator command, sf-cio-prepare-dock-usb,
through the existing serialised command gate. It binds the selected endpoint
then retries matching; driver startup requests checked late DMA admission.
Default/network-only profiles still reject PCI USB matching. No boot
collection selects this opt-in pair yet.

Actual transformed guard, extended-capability/BIOS-handoff, quiesce and
retirement methods pass ASan/UBSan host fixtures, including inaccessible
status, worker/operation/reset timeouts and completion failures. These are
host fixtures, not native IRQ joining or hardware producer-drain evidence.
Both opt-in native profiles and the separate command tool build. Actual
catalogue policy fixtures exercise network-only rejection, exact unique USB
matching, and refusal of broad/extra personalities. Real M1 qualification,
including controller startup and IRQ/work retirement, remains before support
can be reported.
