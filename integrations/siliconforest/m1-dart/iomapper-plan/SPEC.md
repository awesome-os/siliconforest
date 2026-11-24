# T8103 per-requester IOMapper integration contract

Status: source-grounded design for review. No adapter, hardware backend, SDK
build, registration, MMIO, DMA or initialized DART is supplied by this document.
Root chooses the implementation after review. The frozen codec and survey stay
unchanged. In particular, the survey's PHY refusal is not authorization to
remove its guard or to run `pcie_init`.

## 1. Scope and the blockers that determine the design

Use the original C codec with one bounded, non-system mapper for the native
J293/T8103 WLAN requester. The first eventual mapping profile is **whole,
privately owned, wired 16K RAM pages**, with no aliases and no firmware-table
adoption. It is not a general-purpose replacement for Apple's DART driver.
It does not yet make the Wi-Fi driver, its firmware or its DMA usable.

Three caller facts are decisive:

* `IOMapper::start()` calls the subclass's `initHardware()`, then records page
  size and copies the provider's `iommu-id`, or failing that `AAPL,phandle`, to
  `IOMapperID`. It does **not** call `registerService()` [X1].
* `IOGeneralMemoryDescriptor::dmaCommandOperation(kIOMDDMAUnmap)` invokes
  `dmaUnmap()` but returns success regardless of that error [X3:3317-3327].
  `IODMACommand::complete()` also clears its local allocation fields after
  calling unmap, and `clearMemoryDescriptor()` can then release its descriptor
  [X4:455-471,1087-1114]. A failed invalidation cannot be made safe merely by
  returning `kIOReturnTimeout`.
* A missing explicit mapper can select the global mapper or leave a command
  operating on physical addresses [X4:289-337,428-441]. A failure-to-start
  mapper is not, by itself, a DMA admission gate on a PCI device.

Required external mechanisms remain absent: normal-DART MMIO reachability and
power/clock lifetime, exclusive stream/RID ownership, device transaction drain,
table cache maintenance, completed invalidation, fault containment, and a PCI
host admission gate. Until these exist, `initHardware()` must fail before any
register access and the service must never publish as an available IOMapper.

## 2. Exact pinned interface

The declarations below describe the future class; they are not an implemented
or compiled adapter. XNU pin is `e7aa00a60f0b56ef35aa24252e81467787bf71ea`.
All addresses and lengths in these virtuals are **bytes**, not page numbers.
The old C system-mapper wrappers convert page numbers separately [X1:229-279].

```cpp
class PDT8103WLANMapper : public IOMapper {
    OSDeclareDefaultStructors(PDT8103WLANMapper);
protected:
    bool initHardware(IOService *provider) override;
public:
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    void free() override;
    uint64_t getPageSize(void) const override;
    IOReturn iovmMapMemory(IOMemoryDescriptor *memory,
        uint64_t descriptorOffset, uint64_t length, uint32_t mapOptions,
        const IODMAMapSpecification *mapSpecification,
        IODMACommand *dmaCommand, const IODMAMapPageList *pageList,
        uint64_t *mapAddress, uint64_t *mapLength) override;
    IOReturn iovmUnmapMemory(IOMemoryDescriptor *memory,
        IODMACommand *dmaCommand, uint64_t mapAddress,
        uint64_t mapLength) override;
    IOReturn iovmInsert(uint32_t options, uint64_t mapAddress,
        uint64_t offset, uint64_t physicalAddress, uint64_t length) override;
    uint64_t mapToPhysicalAddress(uint64_t mappedAddress) override;
};
```

`initHardware`, `getPageSize` and the four mapping virtuals are pure virtual in
the pin [X2:86-131]. `start`, `free`, and the inherited stop/termination policy
also need deliberate ownership handling. Additional unload/termination methods
must use the actual SDK declarations when implemented, not invented signatures.
The kernel-private `fPageSize`/`fAllocName` fields must not be accessed by copying
a private layout into the kext. The base class manages them. Set inherited
`fIsSystem = false` before calling `IOMapper::start()`.

`getPageSize()` returns the software profile size `0x4000`; this alone is never
hardware readiness. The IOKit KPI export list includes the IOMapper base class,
start/free, constructors, vtable and copy-mapper APIs [X7]. A future real SDK
build must check actual imports and SDK visibility of `IODMAMapPageList` and UPL
accessors; no link or target ABI proof is claimed here.

## 3. Matching, publication and the device gate

Saved actual guest ADT identifies:

| Node | Role | phandle / property |
| --- | --- | --- |
| `/arm-io/dart-apcie0` | Hardware instance | `AAPL,phandle = 0x4f` |
| its `mapper-apcie0-wlan` child | WLAN mapper identity | `AAPL,phandle = 0x50` |
| its `mapper-apcie0-bt` child | Separate BT mapper identity | `0x51` |
| `/arm-io/apcie/pci-bridge0/wlan` | Client | `iommu-parent = 50 00 00 00` |

The WLAN mapper must attach to a genuine nub representing the **mapper child**,
or receive that exact verified identity through an explicitly constructed provider.
Attaching directly to the hardware node and relying on the base `start()` would
publish `0x4f`, which does not match WLAN. Merely setting `IOMapperID` before
`super::start()` is also wrong: the base can overwrite it.

`IOMapperID` must equal the four-byte `OSData` identity, not an `OSNumber`, a
string, a host-endian eight-byte integer, or SID `0x50`. `copyMapperForDevice()`
reads `iommu-parent` from the supplied service, extracts a `UInt32` from OSData,
and waits for that property [X1:174-207]. Its wait defaults to forever [X8].
Therefore a consumer must not invoke it while holding a mapper/controller gate
or use that wait as a readiness timeout. Perform a bounded availability check
outside locks before entering the normal copy-mapper path. If an unpublished
admission-only skeleton is installed, do not publish/attach its dependent DMA
consumer at all.

Before successful publication:

1. Validate exact ADT board/instance, child identity and physical aperture. The
   existing sampler's numerical allowlist does not establish safe MMIO.
2. Obtain real exclusive controller/stream ownership from the future native
   PCI host owner; the survey is not such an owner.
3. Establish all runtime prerequisites in section 8, including held power,
   fault handling, requester containment and empty owned translation state.
4. Call base `start()` only with those prerequisites available to
   `initHardware()`. Verify the resulting `IOMapperID` and page size. If any
   prepublication step fails, release only never-exposed software resources.
5. Call `registerService()` last. Its presence says this bounded profile is
   available, not that a device transfer completed.

Preserve `IOMapper::gSystem` and the platform's mapper-required policy. The
platform expert controls the latter [X9]. Do not set the global mapper-present
property and stall unrelated devices; do not publish a dummy mapper to satisfy
the indefinite wait.

The eventual PCI host/consumer must pass the retained `IOMapper *` explicitly
to `IODMACommand::withSpecification`, require mapped mode, check every map and
prepare result, and admit bus mastering only after genuine readiness. Passing
a non-IOMapper device pointer in the mapper slot is not this contract: the pin
can fall back to `gSystem` [X4:289-295]. Verify the real discovered IOPCIDevice
retains the correct `iommu-parent`; a raw ADT child alone does not prove that
the PCI configurator created the desired nub [X10].

`IOPCIDevice::setBusMasterEnable()` returns the **previous command-bit state**,
not a success/verified-drain result [X11:899-916]. A false return is not evidence
that disabling failed, and a true return is not a verified disable. The PCI
owner needs readback, requester-specific completion and in-flight drain rules.

## 4. Bounded mapping profile and request parsing

Use the current codec unchanged:

* granule `0x4000`; 32-bit IOVA; physical address range `[0,2^36)`;
* ADT aperture `[0x00100000,0xbff00000)`, checked again against actual metadata;
* always exclude the whole MSI-containing page `[0xffffc000,0x100000000)`;
* TTBR0 only, other three roots zero; canonical full-page leaf/table encodings;
* at most 256 exposed leaves (4 MiB), 129 table buffers, 64 RAM and 64 reserved
  intervals; no duplicate physical leaf, shared/orphan table or table/data overlap.

For the first implementation proposal, add a limit of 32 mapping records and
256 **ever-published** leaf pages per mapper lifetime. Use monotonic, nonreused
IOVA reservations until reboot. Freed ranges need not be recycled in this
bounded experiment. Alignment gaps consume IOVA space; active and quarantined
records both consume ownership capacity. Exhaustion returns `kIOReturnNoSpace`
before publication. This cap is a software restriction, not T8103 capacity or
proof that a Wi-Fi firmware workload fits. Expanding it requires new review.

An allocation is a checked half-open extent inside the aperture and the caller's
`numAddressBits` ceiling. Never shift by 64: 64 means no additional bit ceiling,
then intersect with the 32-bit profile. Alignment is a nonzero power of two;
the returned IOVA must satisfy it. Use checked addition/rounding and explicitly
validate the entire last page. Full-page lengths and descriptor offsets are
mandatory initially; no silently enlarged subpage access is allowed.

`IODMAMapSpecification` has alignment, device, options, address-bit count and
reserved fields [X5:181-187]. Refuse unsupported nonzero options/reserved fields.
Do not require `device` to be present: the full-page-list fast path constructs
a new specification with only alignment/address bits and loses the caller's
device field [X3:4965-5003]. Instead bind ownership to the mapper instance and
to a private approved-buffer registry. If `device` is supplied, verify that it
is the same authorized requester. It is not an authorization token by itself.

Accept only explicitly supported `mapOptions`: device-read, device-write, or
both, plus verified `kIODMAMapPageListFullyOccupied` for the page-list path.
Reject device-memory, identity, fixed-address, paging-path and unknown bits;
initially reject the special physically-contiguous page-list encoding as well.
Do not interpret that flag as a plain array of consecutive UPL entries.

Permissions are from the device's perspective: Out -> device read, In -> device
write. The pin's generic descriptor path can request RW unless prepared
read-only; respect the supplied permission request, constrained by the owned
buffer's policy, rather than guessing from a name [X3:4849-4856;
X4:985-995]. Never grant permissions not requested.

Handle both normal caller shapes honestly:

* **Page list supplied:** it is borrowed for this call. Validate count, zero
  page offset, full-page coverage and all actual page numbers using the SDK's
  UPL accessor. Copy bounded physical page numbers into the mapping record;
  do not retain the stack `IODMAMapPageList` or its borrowed array.
* **No page list:** walk the approved descriptor with
  `getPhysicalSegment(..., kIOMemoryMapperNone)` while its backing is held.
  Default mapping can recurse through an IOMMU, so do not use the default
  option. Reject zero/short/unaligned segments and overflow. Cross-check every
  page against the approved backing allocation.

Initially support only descriptors for mapper/driver-owned, kernel-only,
nonpageable, nonpurgeable whole-page buffers. An object `retain()` is not an
independent pin for arbitrary pageable caller memory: callers can complete
and release UPL wiring even while the descriptor object lives. General user
memory, external UPLs and bounce buffers remain unsupported until an independent
backing/pin owner with correct copy semantics is implemented.

On success return a contiguous IOVA byte address and `mapLength == length`.
Physical pages may be scattered. A different nonzero map length selects a
special multisegment caller path [X4:928-1046], which this first profile does not
implement. `memory == NULL` reservation, null-command/system-wrapper use,
partial insertion and `iovmInsert()` all return `kIOReturnUnsupported` before
any mutation; do not fabricate a successful empty allocation.

Outputs remain untouched on a failed map call. The descriptor wrapper may
separately initialize its output; that does not authorize publishing a partial
map. Malformed arguments -> BadArgument/NotAligned; valid unsupported shapes ->
Unsupported; unavailable ownership -> NotReady; exhausted bounded capacity ->
NoSpace. No physical-address or bypass fallback is permitted.

## 5. Backing, tables and the C/PureDarwin boundary

The future original C component should own bounded metadata, IOVA reservations,
immutable proposed images and transaction generations. It consumes the approved
codec and never receives MMIO pointers. Names proposed for implementation are
`plan_map`, `plan_revoke`, `validate_image`, `commit_observed_completion` and
`quarantine`; they are not implemented APIs or hardware acknowledgements here.
Only a real kernel backend may supply a completion observation. Fake host
backends are test fixtures, never linked as a kernel success backend.

The PureDarwin C++ adapter owns retained IO objects, approved backing records,
normal-memory table pages, cache maintenance, an actual controller operation
interface, and publication. `IOBufferMemoryDescriptor` can allocate wired
kernel buffers; a 16K table allocation must additionally have a checked aligned
physical segment spanning the whole page [X12]. A virtual alignment alone is
not physical contiguity. Keep table descriptors and their CPU mapping alive
until hardware reachability is revoked; never use a CPU VA as the PA.

Every map record holds: a unique generation, mapper/stream owner, borrowed-command
identity for matching, retained descriptor/backing owner, exact IOVA/length,
page vector and permissions, allocated table ownership, state and error evidence.
Do not retain the command in a reference cycle (command retains mapper and
memory). A stale raw command pointer must never be dereferenced after completion;
generation plus a still-owned mapping record determines validity.

The RAM/reserved policy must come from a real native memory owner. Include all
firmware/MCC carveouts, page tables, retained handoff allocations and unrelated
owners. The codec checks interval consistency, not whether a Linux-style
physical range or an ADT region is free. Keep scratch on retained heap storage,
not a kernel stack; serialize it and keep inputs immutable for validation.

The codec's byte-wise serialization is for **unpublished normal-memory images**.
Do not call `dld_word_store()` on a live hardware-walked PTE and assume the eight
byte stores are atomic. The backend must use a justified aligned 64-bit publish
store/barrier/cache sequence, or publish only while the owned stream/requester
is truly quiesced. No table pointer or leaf becomes reachable before its backing
and the whole proposed graph have passed validation.

## 6. Transactions and failure ownership

Suggested state machine:

`UNOWNED -> SOFTWARE_READY -> OWNED_QUIESCED -> ACTIVE`

`ACTIVE -> QUIESCING -> OWNED_QUIESCED`; any uncertain hardware step ->
`FAILED_QUARANTINED` (terminal for this boot). `SOFTWARE_READY` is not a registry
publication or hardware permission. No boolean boot argument can authorize the
transition to OWNED; it requires implemented ownership and containment.

Map transaction:

1. Under serialization, validate owner/state, full request, budgets and physical
   page provenance. Reserve record/IOVA and all required table memory before
   any visible mutation. Reject aliases across the entire stream image.
2. Build an immutable proposed graph and run `dld_validate()` on the whole graph.
3. Establish requester quiescence for publication, hold the controller power
   lease, then perform the reviewed PTE/root publication and cache/order steps.
4. Issue stream-scoped invalidation, observe actual bounded completion and
   readback as required by the backend. Only then return the IOVA as success.
5. A pre-exposure failure rolls back software resources. Once any entry/root
   may be reachable, an error cannot free tables or backing until revocation
   and drain are independently proven. Otherwise quarantine and fail-stop.

Unmap transaction:

1. Match exact descriptor/command identity, generation and returned byte
   extent; reject duplicates and partial ranges before touching live entries.
2. Require endpoint completion and a requester drain mechanism. A completed
   software command, bus-master bit clear, fault record or TLB flush is not
   proof that previously accepted transactions are drained.
3. Revoke only owned leaves using the reviewed sequence, synchronize tables,
   invalidate only the owned stream and wait for genuine completion.
4. Release backing and table references only after reachability and outstanding
   DMA have both ended. Keep IOVA generation/history reserved for this boot.

**Failure rule:** an unmap timeout is not safely recoverable by an error return
in this pinned caller stack. On an uncertain revocation/ownership loss, keep
the whole affected stream's tables and backing wired and retained, reject new
maps, and invoke a real containment/fail-stop path before returning to callers
that may free or reuse memory. No timeout-as-success, detach/free, reuse,
automatic reset, power-down or best-effort continuation. A panic/fail-stop does
not stop DMA by itself; retained memory and an actual containment design are
still required. If such retention/containment cannot be guaranteed, publishing
the mapper is forbidden. This is the existing quarantine boundary, not an
exception to it.

## 7. Concurrency and lookup semantics

The DMA caller supplies no global serialization for a non-system mapper. Use a
sleepable transaction gate in process context; reject unsupported interrupt
context rather than allocate or block under a simple lock. Prepare bounded
memory before short hardware critical sections. A controller-wide short lock
must serialize STREAM_SELECT/STREAM_COMMAND with every other stream owner.
Never run descriptor callbacks, property publication, allocation or wait for
an interrupt while holding that short lock. Document a single lock order and
make fault handling independent of locks held by the interrupted mapper.

`mapToPhysicalAddress()` has no error return. The mapped-read and DMA-copy
callers immediately use the returned number as a physical address
[X1:292-390; X4:1190-1222]. Returning zero on an unmapped address is unsafe:
zero is a representable PA, not an error sentinel. Return the exact PA including
the byte offset only for a validated active owned mapping. Invalid lookup or
lookup after uncertain revocation must trigger the explicit fail-stop policy,
never identity translation or a fake zero. The checked C lookup still returns
`DLD_UNMAPPED` normally in software tests; the C++ ABI wrapper cannot hide that
error by inventing a physical address.

Serialize lookup with map/unmap and retain the table image through the lookup.
Returning a PA cannot hold a lock through a caller's later `copypv`; callers
must retain the prepared command/owned backing and must not race complete with
copy/segment operations. The adapter cannot promise safety for an invalid
concurrent caller simply by locking its table walk. Reject unload/termination
while published or while any active/quarantined ownership exists; `free()` is
not a quiescence mechanism.

## 8. Hardware contracts still required before any implementation can activate

* **Safe access and stable power:** resolve the normal DART clock/reset gap in
  the separate prerequisite review. A successful read-only survey, if later
  obtained, would still not grant write ownership or a stable power lease.
* **RID/SID:** `0x50` is mapper identity, never a SID. Select a real free SID
  0..15 and RID2SID slot 0..63 only after actual complete-table inspection and
  owner agreement. Bind a full observed bus/device/function RID; do not infer
  RID0x100/SID1 from Linux. Its bus-masked shared WLAN/BT policy is deliberately
  unsupported by the codec's exclusive-SID model. Preserve unrelated roots,
  remaps, requester slots and BT. Do not clear all hardware slots to find one.
* **MSI:** exclude the entire 16K page containing `0xfffff000` even though it
  already lies above this ADT aperture. Linux treats its doorbell as a reserved
  MSI region [H2:2631-2645]; the codec does not map it. MSI interception,
  forwarding, AIC routing and any special doorbell treatment belong to a
  separately qualified PCI host/interrupt owner. No ordinary RAM leaf, bypass,
  guessed identity mapping or x86 fallback may satisfy that work.
* **Cache/TLB:** ordinary RAM is not automatically coherent to the DART walker.
  Implement and justify table clean/visibility, barriers and device-write
  ordering for this XNU/m1n1 environment. CPU TLB invalidation is not DART
  invalidation. Pinned m1n1 writes selected stream, does `dma_wmb`, commands
  invalidate and polls BUSY, but its void helper only logs timeout
  [H1:137-146]. Do not copy that success behavior. Linux serializes and returns
  the command timeout [H2:1089-1122]. Table-walk invalidation is not an endpoint
  completion fence.
* **Fault:** AIC696 is the saved ADT interrupt, not a registered handler.
  Establish real delivery before DMA. The T8020 reference reads ERROR and fault
  address, extracts SID, then acknowledges ERROR [H2:2677-2711]. Acknowledge
  only under owned-controller semantics; capture private raw evidence first.
  Stop admission, retain reachable memory and contain the requester on faults.
  Do not convert a fault into an empty-map success or restart a shared block.
* **Lifecycle:** no hotplug, sleep, reset or unload support in the first bounded
  profile. Those operations must be prevented by real owner policy or lead to
  explicit containment, not nominal success. Disable admission before losing
  clock/control ownership; join callbacks before freeing never-exposed software.

## 9. Smallest safe next implementation, if root selects it

Implement a bounded C allocation/transaction planner against fake operations
first, then an admission-only C++ boundary whose concrete ownership provider
is absent. That boundary returns false from `initHardware()` and never calls
`registerService`, installs a mapper ID, enables a requester, or accesses MMIO.
Its mapping methods return NotReady/Unsupported without mutation; the numerical
page-size accessor is merely the profile. No fake backend is a production path.

Do not add an Info.plist personality, CMake target, KC member or consumer change
until root selects that source scope. A permanently failing class is only an
admission test fixture; it must not be presented as an implemented native mapper.
Activation is separate work gated by the unresolved contracts above. The useful
first deliverable is the planner and negative-boundary tests in TEST_PLAN.md.

## Source index

All X paths are relative to `sources/puredarwin-baseline/` at exact e7aa00a.

* X1: `src/Kernel/xnu/iokit/Kernel/IOMapper.cpp`.
* X2: `src/Kernel/xnu/iokit/IOKit/IOMapper.h`.
* X3: `src/Kernel/xnu/iokit/Kernel/IOMemoryDescriptor.cpp`.
* X4: `src/Kernel/xnu/iokit/Kernel/IODMACommand.cpp`.
* X5: `src/Kernel/xnu/iokit/IOKit/IOMemoryDescriptor.h`.
* X6: `src/Kernel/xnu/iokit/IOKit/IODMACommand.h`.
* X7: `src/Kernel/xnu/config/IOKit.exports:1414-1429,1789-1790,1868,1925`.
* X8: `src/Kernel/xnu/iokit/IOKit/IOService.h:948-956`.
* X9: `src/Kernel/xnu/iokit/Kernel/IOPlatformExpert.cpp:145-157`.
* X10: `src/Kernel/Extensions/IOPCIFamily/IOPCIBridge.cpp:2040-2085,2220-2354,2430-2462,2609-2617`.
* X11: `src/Kernel/Extensions/IOPCIFamily/IOPCIDevice.cpp:899-916`.
* X12: `src/Kernel/xnu/iokit/IOKit/IOBufferMemoryDescriptor.h:196-218`.
* H1: `sources/m1n1-baseline/src/dart.c`, exact121fcbf.
* H2: `sources/linux-aurora-platform-reference/drivers/iommu/apple-dart.c`,
  acquired exact6a08a08e; behavioral reference, no imported GPL implementation.
* C1: `integrations/puredarwin/m1-dart/dart_codec.h`, `dart_codec.c`, README and
  revision002 source-validation. Codec production bytes are unchanged.
* Saved metadata: private survey001 `guest/guest-adt.bin`, SHA256
  `8f8613d8787f912137b482991dc0d5eca403d93e01bceb212019a02be9b8d8c5`.

Original planning text; original-code license remains pending. No upstream
code is relicensed. This document reproduces required interface declarations
for compatibility and distinguishes proposed policy from pinned implementation.
