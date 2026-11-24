# BCM4378 host-only composition test

This test joins the existing original fake transport, identity/firmware-name
policy, software DART mapping, logical interrupt source and shared-ring models.
It adds no production implementation, kernel backend, boot packet or hardware
authorization. All register reads and permitted endpoint writes access ordinary
arrays owned by the test. Shared APCIE PHY and PMGR writes remain compiled out.

Run on the healthy Linux builder:

```sh
python3 integrations/puredarwin/m1-apcie-bridge/bcm4378_integration/test_pipeline.py
```

The healthy fixture selects fabricated revision-5 metadata using an explicitly
unverified numeric chip profile. A callback pretends one executable filename is
present and board data is absent; it never opens files or supplies firmware
bytes. The shared-memory handoff is preseeded by the fixture, not produced by
firmware. The transport stays in `BCT_SHARED`: the separate transport mailbox
setup/post APIs change phase and are intentionally not combined with this ring.

One test-owned context holds exactly one RID/SID mapping, one interrupt source
and one ring. The ring's numerical label equals an IOVA checked by the actual
software DART codec. Neither that label nor its resulting numerical PA is
converted to a pointer. The fake descriptor refers to a numerical label; a
separate ordinary byte buffer holds the synthetic item. This does not prove a
physical buffer is mapped, accessible or coherent. The device read permission
is checked and device write permission is refused.

Submission checks buffer-before-index-before-doorbell order through the real
fake ring implementation. A direct test write advances the synthetic consumer;
an explicit test call injects a logical interrupt. Dispatch returns arithmetic
vector/AIC identifiers only. None of these operations is firmware execution,
DMA, MSI delivery, AIC acknowledgement, Wi-Fi traffic or a driver completion.

A test-only PHY guard precedes the endpoint path. A reset-state fixture must
stop after one PHY read, with zero endpoint accesses, register writes, firmware
callbacks, mappings or interrupt/ring binding. This is a useful composition
rule, not a new hardware reachability proof. It does not change the narrower
contracts of the predecessor models.

After healthy logical close, the context, DART pages, ring descriptor and item
buffer remain retained, and a second ownership claim is refused. A reset after
producer publication keeps the dirty/faulted ring and its backing, prevents the
doorbell and blocks retry. Pending logical interrupts prevent close. Every
callback on the healthy path is also failed independently, with both
before-application and after-application failed-write behavior; no failed run
may claim a completed lifecycle. The next fault case resets the entire ordinary
RAM fixture. That reset is test isolation, not a recovery API.

The test's single-context owner is not a production ownership allocator. The
predecessors do not enforce cross-object requester/vector exclusivity. Storage
lifetimes, serialization and nonaliasing remain caller obligations. There is no
DART unmap, real quiescence or safe physical release mechanism here. IRQ I/O
failures and DART I/O failures have different retry contracts; the composing
test stops on either instead of inventing a common recovery policy.

The runner checks the explicit fake build under ASan/UBSan, default and
host-compiled `KERNEL` zero-I/O gates, NDEBUG/fake-kernel compile refusals, and
six compiled mutations of the test orchestrator. These mutations test the
composition assertions; they do not replace the independent predecessor
mutation suites or establish complete coverage. The test is not included in a
Darwin SDK or kernel collection build.

The real readonly PCIe observation remains separately recorded as
`PHY_NOT_READY` with an unsuccessful aggregate qualification and verified Linux
recovery. This host test does not change that record, permit initialization or
authorize another hardware cycle. No source is copied from Linux/macOS drivers;
the test consumes the existing original APIs and their documented contracts.
