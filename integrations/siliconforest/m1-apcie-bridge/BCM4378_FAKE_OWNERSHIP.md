# Fake endpoint ownership and cleanup, revision 001

This original host-only layer composes the unchanged fake DART and interrupt
sources. It is a serialized software registry, not an IOMapper, MSI controller,
allocator, hardware ownership protocol or implementation of a platform plan.
No upstream source was fetched or copied for this slice.

Use one canonical `bfo_registry` for the fake endpoint. Every participating
caller must use it, serialize all operations and retain stable, disjoint
objects/backing/policy arrays. Direct calls to the old APIs or creating another
registry can bypass this cooperative contract; it is not a security boundary
or a concurrent kernel adapter. Registry and handles start zeroed and must not
be copied, moved or edited while active. Closed handles cannot reopen. A new
object may claim a resource after its prior owner has successfully closed.

The registry permits one exact RID `0x100` binding with an explicit SID `0..15`.
A second mapping is refused even with a different SID, so the same requester
cannot acquire ambiguous stream bindings. Each of the eight logical interrupt
vectors has its own exclusive owner; other free vectors may have other owners.
Copied handles fail pointer-identity checks. Registry close requires both the
binding slot and every vector slot to be empty. No cleanup is automatic.

`bfo_map_pages` uses the existing DART codec and validator through `bfd_map_pages`
with extra caller-owned inactive staging. A final PHY/link/identity guard occurs
before either owned table page changes. Internal image pointers refer to the
stable destination, not temporary stack storage. Translation stages its output
and also rechecks before returning a PA label. These are ordinary host RAM
operations; none is a real device translation, cache operation or DART write.

`bfo_map_close` is the new software close/unmap: on a healthy, nonfaulted owner it
zeros previously mapped fake table bytes, removes the image and releases the
registry claim. The map object and caller backing remain retained and are not
freed. I/O faults during an active mapping, invalid table encodings and topology
corruption latch `map.faulted`. Every later map/translate/close refuses without
retry, and the binding/backing remain retained. No reset, forced close or fault
clear API exists. Invalid ordinary request arguments do not become successful
maps and do not in themselves assert a hardware fault. A failed open before
claiming an owner leaves the registry and output handle unchanged.

Interrupt operations stage the unchanged `bmi_*` source before a final guard
and observable commit. Pending/coalesced event and mask rules remain unchanged.
Close refuses pending events, unmasked sources, a down link or a reset PHY;
failure does not release the vector or silently discard events. Explicit fake
receive, unmask, one-event dispatch, mask and close are separate steps. No real
MSI is sent or acknowledged, and no T8103 MSI register layout is introduced.

All eleven operations check or refuse ownership before mutation. For an active
fake device the readiness gate samples PHY reset/clock ACK, RUN/link, fake PCI
identity and PHY again. Initial down/reset refusal preserves handles, registry,
owned table bytes, scratch and outputs, with zero register writes. A late
failure can leave private inactive scratch changed, but cannot commit owned
table/interrupt/output state; active DART I/O errors instead retain their sticky
fault fact. Caller serialization is required between the final sample and CPU
commit. This is not an atomic physical-hardware exclusion guarantee.

Working builds require `BFO_FAKE_DEVICE`, `BFD_FAKE_DEVICE`, `BMI_FAKE_DEVICE`
and `APB_HOST_TEST_ACCESSOR`. Default and host-compiled `KERNEL` operations only
return `BFO_GATED`, without callbacks or object/output changes. Kernel+fake and
shared PHY/PMGR writer opt-ins are compile-refused. GPIO 152 and SMC radio
commands remain compiled out. No firmware bytes, DMA, real allocation, DART/MSI
register programming, `P_PCIE_INIT`, physical mapping, kext/KC or boot packet.

Run `python3 integrations/puredarwin/m1-apcie-bridge/test_bcm4378_fake_ownership.py`.
ASan/UBSan tests use the exact prior IRQ fake register file. Its old main is
renamed and never run. Tests cover exclusive/released/retained claims, all eight
vectors, pending/unmasked cleanup refusal, image teardown and destination
pointers, all initial down/reset states, late reset before commit, every read
fault point and sticky retries. Counted mutants compile and fail assertions.

The real observation is still PHY_NOT_READY. This slice supplies no actual
chip/BAR/DMA/firmware-load evidence and does not relax any hardware guard.
