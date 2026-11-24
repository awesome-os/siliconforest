# Original BCM4378 fake shared-memory ring and doorbell

This is a source/host-only model atop the unchanged fake transport's BCT_SHARED
state and checked endpoint BAR accessor. It supports one H2D ring0 using a
caller-preallocated ordinary byte array, depth2..16/item4..64bytes, total<=1024.
There is no real ring allocation, firmware byte, DMA, physical mapping, kext,
MSI layout, IRQ registration or boot path. Payloads are opaque synthetic test
items; no actual msgbuf header or firmware command is claimed.

The pinned ISC `brcmfmac/pcie.c` is a protocol facts map only. The source is
original, not copied Linux code. Facts used: ring-info prefix words contain
ring memory and H2D write/read-index addresses (450-478,1398-1430); descriptors
are16bytes, depth/item length at+4/+6 and64-bit buffer label at+8 (309-312,
1268-1320); TCM indices are16bit values at4-byte strides (1420-1430); producer
publication precedes ringing BAR0+0x2000+0x140 or DAR+0xa20 with value1 (1197-1230).
No GPL source, including pcie-apple.c, is read or copied.

Open validates bounded/disjoint shared prefix, ring-info, descriptor and index
ranges against the explicit fake RAM geometry, excluding mailbox/tail metadata.
It reserves120bytes around shared metadata (covering mapped header fields
through+116) and a conservative64-byte ring-info guard band. The latter is a
strict software policy, not an inferred exact C structure ABI. Protected spans
must also be mutually disjoint; descriptor/indices cannot overlap either band.
DMA-index mode, changed shared metadata and foreign/overlapping pointers refuse.
Configure refuses an already populated descriptor or initial nonzero indices;
it serializes the supplied symbolic64-bit label first, then depth/item metadata,
and verifies readback. That label is never a host pointer or proven DMA address.
No allocator is called and the preallocated fake buffer is not initialized or
replaced by configure. Descriptor word0 is preserved.

Submission checks current host/consumer indices and leaves one slot empty.
FULL does not overwrite unread items. The order is ordinary fake-buffer copy,
low16 producer publication, checked readback, then the BCM endpoint doorbell.
The adjacent index halfword is preserved. Because the existing fake accessor
has word access, the16-bit update is simulated by a serialized32-bit RMW.
**That RMW is not admitted as a safe real-device width contract.** A future
physical adapter needs genuine16-bit accesses and real DMA/cache barriers.

Every API, endpoint read, write and buffer-copy commit checks fake PHY reset/
clock request-ack readiness, RUN/LINK and PCI14e4:4425; PHY is sampled again at
the end of the guard. Entry link-down/reset leaves descriptor/index/BAR state,
buffer and ring object unchanged with zero new writes. No PHY/PMGR write exists.
No raw T8103 MSI register is invented: the doorbell is the BCM function's BAR0
ring notification from the ISC map, distinct from its config0x98 mailbox kick.

Partial mutations retain dirty/inflight/fault facts, including failed writes
that may have applied. Index-published counts readback-confirmed updates only;
an attempted index write may apply even if a later refusal prevents confirmation.
Doorbell-attempt counts include attempting its guarded operation; only the fake
backend log shows whether a write was issued/applied. No retry or rollback is
automatic. Faulted objects cannot submit or claim cleanup. Close requires an
observed empty ring and only marks logical retirement, retaining descriptor and
buffer. This is not real firmware/DMA/IRQ quiescence or permission to free pages.

All objects and metadata have one serialized caller with stable, disjoint
storage. Fake consumer progress in tests is explicit host manipulation, not DMA.
The parent must remain BCT_SHARED with stable metadata/window, and its setup in
tests runs only while the fake PHY/link are healthy, before measuring this
slice's writes. No old receipt is rewritten or hardware survey rerun.

Only BCR_FAKE_DEVICE plus the checked host accessor enables the model. KERNEL+
fake and the shared PHY/PMGR fake-write escape are compile errors. Default and
KERNEL APIs return BCR_GATED with zero I/O/buffer changes; GPIO152 and SMC command
flags remain0. No source recipe, lead port packet or target candidate is edited.

Run `python3 integrations/puredarwin/m1-apcie-bridge/test_bcm4378_fake_ring.py`.
ASan/UBSan cover normal/DAR doorbells, descriptor order and64-bit labels, index
neighbors, FULL/wrap, pointer/ownership/width failures,28 reset/down/clock cases,
five late-reset cases (including the final pre-copy guard), and every callback
fault before/after fake write application.
Fourteen compiled assertion-killed mutants and negative builds are included.
Leak detection is disabled for the host environment. Passing proves only this
bounded fake data path; no radio, DMA, real ring or boot admission follows.
