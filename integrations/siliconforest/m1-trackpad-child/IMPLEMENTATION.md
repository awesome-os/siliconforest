# Passive T2 kernel source candidate001

Source-only implementation of reviewed DESIGN002. No SDK, KC, hardware,
pointer-input or physical-panel result is claimed. Root owns those stages.
The approved frozen T1/keyboard sources, existing packets and host remain intact.

`adapter/` overlays the exact composed T1 adapter001 plus core002 inputs.
`source-manifest.json` supplies every destination/source/hash for a separate
build directory. `trackpad-child.patch` applies to that already composed T1
directory with `patch -p1`; its output is checked against the owned adapter
files. It does not patch an active checkout. Root supplies the unchanged
shared ABI header and the private generated reference header listed in the
manifest. The reference is comparison-only; the child copies its descriptor
from the current-boot successful acquisition.

The bundle identity and Info.plist stay unchanged. There is no matching
personality for the programmatically attached SFM1SPITrackpad. The three boot
arguments are `sfm1kbd=1 sfm1trackpadenum=1 sfm1trackpadchild=1`; the child token
is parsed before the global single-attempt probe claim. Without it, the parent
keeps T1 registration timing and base open/newUserClient behavior. The source
test compares frozen startHardware/enumerate/enumerateTrackpad/waitResponse/
receive/preserveTrackpadEnumeration bodies exactly.

Both HID devices suppress base-start registration on T2. Child preparation
failure records a failed T2 result but permits the existing keyboard-only
fallback if parent commit succeeds. Timer failure registers neither device.
After commit there is no failed-start rollback: explicit parent and child pins,
plus childOwned, retain the cohort until reboot. Stop and destruction guards
cannot release the loop, mappings or HID state. Child retirement is recursive
sink clearing without waiting, sleeping or draining. A stop without a provider
still removes the child's old ready property; the child never looks up a loop
after detach or uses an async report API.

Fault publication is best effort and truthful: a failed immutable snapshot
replacement removes the old ready properties, preserving the primary cause in
the in-memory state and a separate numeric publication-failure log. Independent
reads are not a coherent transaction or liveness lease. The child property must
pass the separate registry gate; IOLog/service presence cannot replace it.

The signed32 `last_error` table uses actual target IOReturn values:

| Cause | Stored value |
|---|---|
| Acquisition/descriptor identity admission failed | `kIOReturnUnsupported` |
| Child init or report/snapshot allocation failed | `kIOReturnNoMemory` |
| Boolean attach/base-start failure lacking a specific return | `kIOReturnError` |
| Timer arm / gated commit refusal | Exact returned IOReturn cast to int32 |
| Synchronous IOHID report failure | Exact returned IOReturn cast to int32 |
| Parent transport failure | Existing parent's actual signed error |

The first nonzero cause is sticky. Normal retirement keeps zero if no fault
preceded it. Unsupported get/setReport calls and dropped ID/length candidates
do not modify this field, fault the route or write transport state. They are
separate local refusal/counter paths. Input reports require exactly 8/17/1752
bytes for IDs02/3f/44, including the ID. Nothing pads, truncates, strips a
prefix, interprets vendor touch data, sends mode/feature/output/haptic requests
or expands the existing2038-byte payload bound.

Run the host-only checks:

```
python3 integrations/siliconforest/m1-trackpad-child/test_child.py
python3 integrations/siliconforest/m1-trackpad-child/verify_source.py
```

The C++ harness compiles the complete child, full new parent lifecycle include
and verbatim extracted parent init/probe/start/poll/stop/free/report bodies.
Fake IOKit supplies allocation failures, base-start self-stop, recursive gate
execution, matching callbacks, attachment references and retained-loop lifetime
checks. It deliberately retains fake allocations for inspection; it is not a
real scheduler, SDK ABI test, kernel parser or quiescence proof. Enumeration
is explicitly fake in this harness. The C policy test uses the real unmodified
wire engine, synthetic packets and both CRCs. UBSan, strict warnings and
NDEBUG compile refusals are active. Seven mutants must compile and then fail
an assertion, not merely fail compilation.

Original T2 code is newly authored, license pending as required by AGENTS.
Inherited parent/CMake code keeps the frozen T1 provenance; no Linux driver
implementation is copied. Primary behavior comes from pinned SiliconForest e7aa00a
IOHIDDevice/IOService and the approved actual descriptor interpretation.
Independent implementation review and root's real SDK/KC/packet checks remain
required. This candidate intentionally provides no unload/reclamation support.
