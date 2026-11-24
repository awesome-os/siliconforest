# Test plan for the proposed bounded IOMapper

These are acceptance tests for future implementation, not tests run by writing
this plan. No fake completion may be linked into a production kernel backend.
Host observations are software-policy evidence only. SDK, runtime and physical
device claims require separate receipts and root authorization.

## A. Pin and interface checks before implementation

1. Bind the exact e7aa00a IOMapper and DMA caller files recorded in review.json.
   Generate/compare the concrete subclass declarations against IOMapper.h:
   return types, constness, argument order, byte widths and virtual qualifiers.
   Do not substitute a modern macOS SDK header.
2. Check source ordering: fIsSystem=false before base start; initHardware false
   prevents mapper-ID publication and registerService; no writes to gSystem or
   platform mapper-present policy. Refused service start leaves no successful
   registration or enabled bus-master path.
3. A property-matching harness uses actual copied four-byte OSData semantics:
   WLAN0x50 matches; hardware0x4f, BT0x51, OSNumber0x50, string"0x50", eight-byte
   data and malformed iommu-parent refuse. Test base-start provider copying so
   pre-setting the right ID followed by copying the wrong provider fails.
4. The full page-list caller may omit mapSpec.device. Test that a genuinely
   approved owned buffer still works through that path while an unregistered
   descriptor with an absent/spoofed device cannot acquire authority.
5. Event-driven startup must never call the indefinite copyMapperForDevice wait
   under the operation gate. Source checks bind this to the real default timeout
   in IOService.h; absence of a mapper must not create a bypass command.

## B. C planner: table-driven boundaries and a separate reference model

Run the original codec's unchanged checks first. The new planner must use the
real codec, not a duplicate encoder or a test-only successful translator. Use
ASan/UBSan and explicit NDEBUG refusal for action-bearing asserts. No sanitizer
failure is an assertion-mutant kill. Host temporary files remain in the allowed
test directory; no upstream tool or transport module is imported.

Required cases:

* 16K first/last valid pages in the ADT aperture; exactly-one-past, overflow,
  zero length, zero/non-power-of-two alignment, PA36/IOVA32 high-bit rejection.
  Address bits0, small values,32,36,64 and >64 without undefined shifts.
* Entire excluded MSI page, including bytes before/after the 4K doorbell and
  a range crossing its boundary. A larger explicitly supplied test aperture
  must still exclude it; the current smaller aperture must not mask this test.
* 255,256,257 total published pages; all32 records versus33; release does not
  replenish the lifetime quota or reuse IOVA. Live, pending and quarantined
  records participate in the right budgets. Alignment gaps and fragmented
  normal physical pages remain bounded.
* One page, multiple pages and a physically scattered descriptor that maps to
  one contiguous IOVA extent; return mapLength exactly the requested full-page
  length. No partial coverage, silently rounded authority or allocation-only
  success. Empty/null memory and insert requests refuse without side effects.
* R/W/RW leaf permissions, unknown flags, identity/fixed/device/paging flags,
  malformed page-list count/offset, contiguous-list special encoding, and all
  specification reserved fields. Preserve every output sentinel on failure.
* Descriptor-derived page vector must equal the approved allocation. Reject
  forged physical pages even when they fit ordinary DRAM. Reject carved-out,
  table-backed, firmware-owned and unrelated-buffer pages.
* Physical aliases within one request, between concurrent maps, between a
  proposed map and quarantined pages, and table/data aliasing. Alias checks
  cover the whole codec image, not only the newest request.
* Every first/last table index, parent/root publication, orphan/shared tables,
  reserved bits, physically valid but unowned table buffers and a mutated
  source image between validation and commitment. No firmware-root adoption.

Use a simple independent model of half-open IOVA extents, retained pages and
mapping generations to check randomized sequences. Compare ownership and
budgets after each step. Never treat the model's acknowledgement as hardware
fact; all operation traces must be labeled fake.

## C. Transaction ordering and exhaustive fault injection

The fake backend records allocate/retain/validate/clean/barrier/publish/
invalidate/requester-drain/release events with owned stream and generation.
It may never accept a raw-MMIO address; it represents the future interface.
Inject failure at every fallible event and every PTE publication position.

Required invariants:

1. Unavailable hardware owner yields zero hardware events, no registration,
   no output address and no retained leaks. No ready bit can be set by a caller
   boolean or a boot argument alone.
2. Every published pointer/leaf is preceded by complete backing retention and
   immutable whole-image validation. Table bytes are stable and visible before
   a parent/root can reference them. Deliberately split byte-wise live stores
   must be rejected by the backend contract.
3. Map success occurs only after the correct stream's actual completion event
   in the fake model. Wrong SID, stale generation, completion-before-command,
   duplicate completion, wrong register and a timeout never count.
4. Every pre-exposure failure is reversible. Every possibly exposed failure
   retains all potentially reachable backing/tables and enters terminal
   FAILED_QUARANTINED. No address is reused and no descriptor is unpinned.
5. Unmap with no requester drain, BUSY stuck, power loss, ownership loss,
   invalidation failure or a fault racing the final step cannot release memory.
   TLB completion without device drain is insufficient. Test both orders.
6. A later map, close, free, stop, detach or simulated unload cannot clear a
   quarantined owner. No reset/power-down retry occurs implicitly. Failure
   instrumentation is bounded and does not print user data or page contents.
7. No operation changes an unrelated SID, RID2SID slot, BT mapper or global
   enable/bypass setting. First-entry fault and shutdown paths obey the same rule.

Counterexample mutations must include: skip owner gate; wrong child ID; infer
SID from0x50; omit one retained reference; grant RW for read-only; disable MSI
exclusion; permit duplicate PA; truncate high bits; return mapLength0; publish
before validation; treat BUSY timeout as success; free after unmap error; clear
quarantine on close; reuse IOVA; return identity/zero for lookup failure; write
all-stream invalidation; ignore table cache visibility. Each applicable mutant
must fail for a semantic assertion, not merely compile or sanitizer failure.

## D. Pinned consumer failure semantics

Use a source-preserving extracted caller harness with narrow IO object stubs,
or a later actual SDK/kernel test. Record which source body is executed; do not
replace the consumer with a more convenient assumed API.

* A failing IOMapper unmap must demonstrate that the pinned
  IOGeneralMemoryDescriptor unmap operation returns success. The harness then
  attempts descriptor completion/release. The mapper-owned backing must remain
  held and the containment/fail-stop event must occur before unsafe return.
* IODMACommand complete must demonstrate that its local allocation state is
  cleared even on failure; clearMemoryDescriptor must not release the mapper's
  independent ownership. This test distinguishes retention of a C++ descriptor
  from actual wiring/ownership of its underlying pages.
* mapToPhysicalAddress is used as a PA by the real transfer/read consumers.
  Unmapped/stale lookup must invoke the failure boundary; it must never cause a
  fake physical read at0 or at the input IOVA. Valid offsets within an owned
  page translate exactly, including the final byte.
* Generic mapper-null fallback, mapped versus bypassed/unmapped mode, and
  setBusMasterEnable's previous-bit return semantics are tested explicitly.
  A checked false/true Boolean alone must not authorize DMA or prove drain.

## E. Concurrency, callback and publication tests

Use deterministic barriers to cover two maps of the same PA, map versus unmap,
lookup versus revoke, two unmaps, IRQ fault during command polling, late fault
after completion, and stop while a callback is pending. Verify generation and
reference counts against the model at each boundary. The IRQ path cannot wait
on a sleeping operation gate or a spinlock held by interrupted code; callbacks
cannot publish services or allocate under the short controller lock.

Test the caller rule separately: a valid prepared command retains backing
through physical copy. A deliberately invalid copy/complete race is not cured
by a table-walk lock; it must be refused or documented as a caller violation,
never labeled safe. Test that exactly one stream owns each selector/command
sequence and that no callback can join a freed owner.

## F. Later SDK and runtime gates (not authorized or performed here)

1. Root builds the actual kext against the pinned headers, checks ARM64 ABI,
   required virtuals, imports/KPI declarations, stack limits, source fidelity
   and personality scope. An admission-only negative build must contain no
   production fake-completion backend and must publish no IOMapper service.
2. Before activation, independently review the real PCI host ownership and
   containment implementation, clock/reset proof, physical page allocator,
   cache/barrier sequence, real invalidate timeout, AIC handler and teardown.
   The saved survey's non-ready result cannot satisfy this gate.
3. Any future root-authorized hardware experiment has an exact packet, one
   qualified requester, no firmware/DMA before admission, private observations,
   and the established Linux recovery. A mapping readback or codec translation
   is not a completed transfer or isolation test.

Unresolved and therefore intentionally untested now: safe MMIO, actual DART
root ownership, in-flight drain, physical coherency, interrupt delivery, MSI
routing, real Wi-Fi buffer demand and device reset behavior. No test count or
PASS result is assigned to these absent mechanisms.
