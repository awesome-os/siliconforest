# Read-only native child identity gate (proposal)

This is a design, not a collector implementation or runtime result. It addresses
Claude's C1 in `trackpad-child-host-contract-preliminary-review.json`. The final
child class and its lifecycle must be settled before code or a candidate ships.

## Required observation

Enumerate `IOHIDDevice` services once. Require exactly two different registry
entries: exact class `SFM1SPIHID` and the exact child class selected by the
reviewed SDK. No subclass-name guess, substring match or command-line class
override is allowed. A separate SFSerialHID match must return no instances.

Read the child's parent iterator in the IOService plane. Require exactly one
parent, whose registry entry ID equals the retained SFM1SPIHID entry's ID.
Require the two device IDs to differ. Mach port numbers cannot substitute for
registry identity. The collected snapshot must not expose raw IDs or names
other than the expected class labels in its public JSON.

Once the child is observed, a null parent iterator or zero parents is an
attribution failure (exit1), not clean absence. A null matching iterator before
observing the child is still clean absence. Do not conflate these cases.

Check both endpoint paths with stat only after this registry observation; never
open either node. The gate itself never publishes anything or claims input.
The new supervisor should first use the existing bounded stat-only endpoint
wait, because publication is asynchronous, and then run this single identity
observation. The collector still checks the endpoints again without opening
them. The host's qualification also requires the actual `ready kind=2` queue line
after BSD readiness. A shared mouse node alone cannot satisfy the contract.

The matching iterator walks a private service-set snapshot; its validity does
not track later registry changes. Parent iterators can become invalid. Keep
both checks without treating either as a liveness certificate.

This is a non-atomic registry observation. It does not prove future service
liveness or exclusive ownership of a shared queue. A service disappearing later
must still make compositor startup fail; readiness is not a pointer report.

## Exact userland surface

Use the already built tiny IOKit archive and its generated `device_user.h`, with
the same SDK flags as collector002. Do not mix in the CF IOKit matching ABI.
The required calls are IOMasterPort, tiny IOServiceMatching and
IOServiceGetMatchingServices, and these generated MIG routines:

- `io_iterator_next` (preserve the returned status)
- `io_iterator_is_valid`
- `io_object_get_class`
- `io_registry_entry_get_registry_entry_id`
- `io_registry_entry_get_parent_iterator`
- `io_registry_entry_get_property_bytes` with a fixed4096-byte in-band buffer
- IOObjectRelease for each owned send right

No IOServiceOpen, IOConnect call, device read, report request, setProperty,
registry mutation, feature query or retry is allowed. Keep all names and plane
strings fixed and bounded; require a terminated exact class name.

The pinned kernel returns kIOReturnNoDevice from `is_io_iterator_next` when it
reaches the end (IOUserClient.cpp:2593). Any other failure is an error, not an
empty iterator. Tiny IOIteratorNext hides this distinction, so do not use that
convenience wrapper for the admission decision. A successful matching call
with a null iterator means no matches, as observed by the earlier inventory
fix. Check non-null iterators for validity before and after their finite walk.

`is_io_registry_entry_get_parent_iterator` returns an IOUserIterator over the
requested plane (IOUserClient.cpp:4254). `is_io_registry_entry_get_registry_entry_id`
returns the entry's uint64 ID (line3661). The exact generated declarations are
in the already built pinned MIG header; do not handwrite competing prototypes.

From the retained child entry, read `ReportDescriptor`, require exactly110
bytes and compare those bytes with the reviewed acquired descriptor (digest
`4ae5bc2626bff5458ba2325e29614f1fec5e8c5a4d32c396f64d59508ec77609`).
Read `SFM1SPIInterfaceID`, selected by lifecycle design002 as an OSNumber(32)
with value2, and require exactly four LE bytes `02 00 00 00`.
The pinned property RPC copies OSData bytes directly; for an
OSNumber it copies numberOfBytes from unsigned64BitValue (IOUserClient.cpp:3708).
On this ARM64 build a32-bit OSNumber is therefore4 little-endian bytes.
Wrong lengths, missing properties, RPC failure and mismatched bytes fail1;
none is a clean missing-child refusal or a substitute successful observation.
This includes NoResources for a missing key, IPCError for a short caller buffer,
NotPermitted from MACF, and successful zero bytes from non-serializable OSData.
The byte RPC does not reveal the property's dynamic type: a four-byte OSData
and an OSNumber(32) can be identical. The kernel setter and its source/binary
binding enforce the selected type; the gate checks only length and bytes.
Do not claim a runtime type check or expand to serialized-property parsing.

## Bounds and result classes

Walk at most three IOHIDDevice results: a third is already an attribution
failure. Walk at most two child parents: a second is already a failure. The
first SFSerialHID instance is a failure. Release every object and iterator on
all exits, including excess-count and malformed-name paths.

The collector makes one finite observation, with no polling or waiting for a
human. Finite RPC count does not guarantee a wall-time bound for each kernel
call; the existing host-enforced cycle deadline and recovery remain necessary.

| Exit | Meaning |
| --- | --- |
| 0 | Exact expected device and parent identities, valid iterators and both character endpoints observed; startup identity only |
| 77 | Clean absence of the expected child/parent or endpoint; not ready |
| 1 | MIG/query error, invalid iterator, extra/unexpected HID device, bad class or provider identity, or malformed result |
| 2 | Bad CLI/nonce before any registry access |

Output must keep `physical_input_proven=false` and `device_opened=false`, report
query errors separately from clean absence, and preserve the actual status.
A nonzero result stops the new supervisor before compositor startup.

The final gate must also read `SFM1TrackpadChildStateV1` from this same retained
child entry: exactly32 bytes in the layout frozen by lifecycle design002.
Require version1, size32, committed1, route_ready1, faulted0, stop_requested0,
last_error0 and reserved0. Reject malformed flags/lengths/version and impossible
states as errors; retain well-formed non-ready states as exit77 observations.
The source implementation must satisfy the matching state-predicate review.

This is the separately frozen T2 diagnostic and requires
committed, route-ready and non-faulted state. Retired or faulted children may
remain registered by the deliberate retention policy. Class/parent identity
and an endpoint cannot replace that state check. The diagnostic's exact schema
must be reviewed with the lifecycle implementation before this gate is coded.

## Verification before use

Use a fake getter only for policy/ownership tests, never as device evidence.
Cover null-success matches, genuine iterator end, MIG failure, invalid iterator,
duplicate IDs, third HID device, SFSerialHID, zero/two/wrong child parents,
malformed class names, missing endpoints and all early-release paths. The SDK
build must bind its real generated prototypes and resolve imports against the
canonical16K-page libSystem. Concrete KC/RAM/source/host binding and independent
review remain required before any target test.
