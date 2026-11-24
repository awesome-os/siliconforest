# Detached firmware-channel source codec

This original pure C leaf encodes and checks a small subset of the pinned
m1n1 V13_5/G13 channel definitions. It has no transport, allocation, mailbox,
shared-memory access, firmware pointer dereference, write, publication or device
operation. It is not a firmware handshake, invalidation engine or GPU driver.

`agxc_select` accepts only explicit `AGXC_PINNED_SOURCE_TEST` evidence and the
exact V13_5/G13 pair. Boot metadata, a requested loaded-image interpretation,
unknown versions and unknown generations return `AGXC_ABI_UNKNOWN`, preserving
the output. Existing captured boot firmware selects a reference profile only;
the loaded GPU firmware image ABI remains unknown. No environment variable or
boot string silently selects a codec here.

The supported detached byte patterns are:

| Channel/pattern | Exact bytes |
| --- | --- |
| DeviceControl DC_Init source pattern | 48 bytes: LE u32 `0x1a`, then 44 zero default bytes |
| DeviceControl UpdateIdleTS source pattern | 48 bytes: LE u32 `0x23`, then 44 zero default bytes |
| FWCtl `send_inval` numeric candidate | 20 bytes: LE u64 address at0, u32 zero at8, u32 context at12, u16 one at16, u16 two at18 |

DC_Init encoding proves no initialization or handshake completion. The optional
UpdateIdleTS zero-default pattern does not sample/update a clock here. The 44
data bytes are unknown fields with an upstream zero default, not established
reserved-zero hardware bits. This subset deliberately refuses other values.
FWCtl has no opcode field: the constants are the pinned sender's numeric
pattern, not a proven invalidation protocol or completion acknowledgment.
Other control operations, including Write32, DestroyContext, jobs and all
event/log/stat formats, are unsupported and fail closed.

FWCtl context is an uninterpreted 32-bit field; every bit pattern is representable
but no value is deemed allocated or safe. Address zero is the source default
with no additional semantic claim. A nonzero address must be 16KiB aligned,
canonical under the restricted pre-G15 40-bit representation, and have a full
16KiB span inside one of at most16 typed synthetic numeric ranges. Range spans
must be aligned, nonempty, nonoverlapping and cannot overflow/cross the
canonical gap. These are deliberately conservative local codec policies;
they are not firmware invalidation-granularity facts. Numeric range coverage
never establishes allocation ownership, ASID validity, accessibility or lifetime.
No payload address is dereferenced or exported to firmware.

The state decoder reads only documented LE32 indices: DeviceControl read@0,
write@32 in48 bytes; FWCtl read@0, write@16 in32 bytes. Other state bytes remain
opaque and may be nonzero. The pinned allocator reserves48 bytes per state
object even for FWCtl; this codec accepts the declared32-byte FWCtl schema,
not a claim about its allocation extent. Each supported ring has256 slots;
strides are48 and20. Indices outside0..255 are refused.

`agxc_ring_candidate` performs bounded arithmetic on an immutable snapshot.
Transmit and consume candidates are local producer/consumer arithmetic, not
proof these host-to-firmware channels are usable as event receive channels.
The one-empty-slot FULL rule is conservative **local policy**: the upstream
sender advances modulo256 without an explicit full check. Equal indices are
treated as empty only within that local policy. Candidates cannot detect an
external full-wrap overwrite, stale snapshot, concurrent producer, DMA ordering
or GPU consumption. No candidate advances indices, queues bytes or rings a
doorbell. This leaf exposes no commit/send/complete API.

All operations require ordinary valid caller-owned C buffers and stable input
facts. Exact wire sizes are mandatory; output/input overlap is refused and
failures leave outputs unchanged. Outputs are detached bytes, scalar fields or
candidate offsets only. They are not reusable hardware authorization. Callers
must serialize their own buffers; this codec creates no lifetime/ownership
ticket. Small locals only, no dynamic allocation or kernel-sized table scratch.

Run `python3 -B integrations/puredarwin/m1-gpu-channel-codec/test_channel.py`.
It verifies unchanged predecessors and six reference files against their pinned
Git objects, reads selected declarations through an original restricted AST
reader, and compares derived schemas with independent literal C golden fixtures.
No upstream Python/Construct module is imported or executed. Host sanitizer and
NDEBUG tests cover exact bytes, every fixed-pattern bit, unsupported profiles,
truncation/oversize, numeric ranges, aliases and all256x256 index pairs for both
channels/directions. Compiled mutants must fail explicit checks. The real pinned
SDK produces one ARM64 relocatable object with no external imports; it is not
linked, installed or target-executed.

Actual loaded GPU ABI, context ownership, initdata, shared rings, CPU/GPU
coherency, mailbox transport and completion remain separate blockers. Frozen
UAT/data/RTKit predecessors are unchanged. GPU observer HOLD remains absolute.
