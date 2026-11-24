# T2 passive native SPI trackpad child — lifecycle design 002

Status: **DESIGN ONLY; awaiting root and Claude review.** No child code, build,
packet, host change or hardware operation is supplied. Root owns
`HOST_CONTRACT.md` and all future host/SDK/KC work. The frozen T1 producer,
collector, captures, keyboard sources and existing native candidates remain
unchanged.

Revision002 supersedes draft001, preserved at `revisions/001/DESIGN.md`
(SHA256 `2926b6df99e3d4bde4cf15b1e712f9e96bc5150492f21cc61f62710a28a680d8`).
The correction is substantive: T2 stages **both** parent and child registration.
It also makes committed retention unconditional, rather than treating a momentary
zero dispatch count as proof that all HID/matching callbacks have quiesced.

## Decision and scope

Use one new programmatically created `SFM1SPITrackpad : IOHIDDevice` inside
the existing SPI owner's bundle. Its provider is the actual
`SFM1SPIHID` instance, never IOResources. The parent remains the keyboard's
IOHIDDevice and the sole SPI/power/GPIO/PIO owner. There is no second transport,
independent child matching personality, child timer or child workloop.

The child exposes the exact descriptor **read from dev2 during this boot**:
110 bytes, SHA256
`4ae5bc2626bff5458ba2325e29614f1fec5e8c5a4d32c396f64d59508ec77609`.
Admission requires the successful T1 observation
(result0/stage5/mask31, descriptor payload and stored length110), interface2,
and exact descriptor identity. A different descriptor is an explicit T2
refusal; do not substitute a bundled descriptor for a failed read. A future
implementation may compare against a fixed 110-byte reference; that reference
is an identity guard, never the source of a fabricated successful acquisition.

**Mode gate remains closed.** No Feature or Output item exists. No child path
may call the SPI encoder, `waitResponse`, GET_REPORT, SET_REPORT, dimensions
query, actuator or haptic operations. T2 uses only the existing five one-use
enumeration keys. It neither enables multitouch nor decodes vendor touch data.

Proposed source opt-in: `sfm1trackpadchild=1`, accepted only together with
`sfm1kbd=1 sfm1trackpadenum=1`, all parsed as the exact value1 before the
existing once-only probe claim. No token, malformed token or missing dependency
must reach T2 creation. Root defines the matching exact host field; this plan
does not alter that contract.

## Grounded constraints

The pinned baseline is SiliconForest
`e7aa00a60f0b56ef35aa24252e81467787bf71ea`.
The composed T1 parent is adapter001
`SFM1SPIHID.cpp 74a22b56...` plus core002. See the separate source/SDK review.

* `IOHIDDevice.cpp:352-378`: base start obtains and retains the workloop,
  installs its event source, calls handleStart, obtains and parses the descriptor.
  `IOService.cpp:1381-1388` inherits the provider's workloop. An attached child
  therefore uses the parent's transportLoop without overriding getWorkLoop.
* `IOHIDDevice.cpp:418-481,499-508,1964-1987`: base start prepares interface
  objects and a first-match notifier, then registers. Setting
  `kIOHIDRegisterServiceKey` false suppresses that final registration.
  The actual SiliconForest property string is **RegisterService**
  (`include/sf_hid_compat.h:58-60`), not an invented HID-prefixed string.
  Interface attach/start occurs later through the first-match callback.
* `IOHIDDevice.cpp:483-487`: a failed base start invokes virtual stop itself.
  A caller must not blindly balance that failure with a second base stop.
* The current T1 parent calls base start at `SFM1SPIHID.cpp:117`, then arms its
  timer at119. Base start may already register the keyboard at
  `IOHIDDevice.cpp:476-478`; the timer-failure cleanup at parent120 does not
  establish that matched keyboard clients have finished. T2 must not inherit or
  widen that window. This is a source finding, not an observed T1 runtime fault.
* `IOService.h:1204-1222`: attach retains both provider and client until detach.
  It is not an open or a transport ownership transfer.
* `IOHIDDevice.cpp:1079-1097,1836-1884` and
  `IOHIDInterface.cpp:502-516`: report delivery calls through synchronously.
  The shared workloop gate is recursive
  (`IOWorkLoop.cpp:483-510,565-576`).
* `IOService.h:741-750`, `IOService.cpp:2828-2859,3417-3459`:
  successful terminate initiation is not unconditional teardown completion.
  Clients can delay finalization, and synchronous termination has a timeout.
  Do not use a true return as permission to free a live callback owner.

These are source facts. No new child has been compiled, attached or run.

## Objects and references

| Resource | Owner and lifetime |
|---|---|
| SPI mappings, packet, reassembly/delivery buffers, timer, transport gate | Existing parent only; no ownership moves to the child. |
| Trackpad descriptor and copied identity | Child-owned immutable copy prepared before child start; no borrowed pointer into the next SPI delivery buffer. |
| Child allocation reference (`childOwned`) | Parent. Separate from the gated routing pointer. Never release it merely because routing is disabled. |
| Gated routing pointer (`childSink`) and readiness | Parent; may be non-null only after child base start and final commit. Clearing it does not release a reference. |
| Shared workloop | Parent allocation reference plus IOHIDDevice's explicit retained references for parent and child. No separately allocated child loop. |
| Per-dispatch report memory | An owned IOBufferMemoryDescriptor copied before the next dlsp_push; released after synchronous delivery returns. |
| In-flight delivery lifetime | Parent and child retained across each submission; numeric dispatch count incremented before entry and decremented after return under the shared gate. |
| Committed lifetime | Exactly one explicit permanent parent retain and one explicit permanent child retain if prepared, taken under the gate after timer-arm success and never released this boot; parent also keeps childOwned. These pins are additional to allocation/attach/dispatch references. Module unload remains refused. |

The child takes no custom raw transport-owner callback pointer. Its stop hooks
use the supplied provider while attached and retained by the service relationship.
They call only a narrow parent operation to clear the matching sink under the
existing gate. They never issue hardware operations.

The child does not open the parent as a HID client. The base handleOpen implementation
tracks the child's own clients; it does not open the SPI provider
(`IOHIDDevice.cpp:794-887`). No console grab, input event or mouse endpoint is
manufactured at attach time.

## Prepare, commit, publish

Child construction must **not** occur in parent handleStart, startGated or the
SPI enumeration gate. Allocation, attach, IOHIDDevice::start, notifier setup,
matching, stop and detach must run on the outer startup thread, outside that
gate. The original keyboard descriptor/filter/report path remains intact.

1. On the T2 path, set the **parent's** `RegisterService` property to
   kOSBooleanFalse and verify the exact stored object before parent
   `super::start(provider)`. Failure to set/read back the property fails startup;
   do not fall through to default publication. Perform existing resource setup
   and base start to obtain the keyboard and dev2 metadata/descriptors. If base
   start fails, it already invokes parent stop; perform only idempotent remaining
   disposal. Neither parent interfaces nor a child have been registered.
2. After parent base start succeeds, validate the completed dev2 observation
   and exact110-byte descriptor. Allocate/init the child and copy its descriptor,
   numeric identity and country. Set and verify `RegisterService=false` before
   attach/start. **Both devices remain staged.** Keep the property false even
   after commit; an explicit registerService call does not need it toggled true.
3. Attach the child to this parent. Call child start outside the gate while the
   parent's poll timer is still unarmed. The inherited workloop is now exactly
   transportLoop. Base start parses the real descriptor and prepares nubs but
   does not call registerService. Child readiness is still false.
4. If child preparation/start fails, record T2 failure, clear its routing pointer
   under the gate, detach if attached and release the parent's allocation reference
   outside the gate. If IOHIDDevice::start returned false, its self-stop has already
   run; do not call base stop twice. A child stopped during this path must never be
   installed as childSink.
5. **Failure policy:** a T2-only preparation failure leaves the existing keyboard
   parent usable if its own startup and timer arm succeed. Parent start then returns
   true, but the distinct T2 result remains failed and no child is registered.
   This is not T2 success: root's registry gate must exit77 before the compositor.
   T1 enumeration/transport failures continue to fail the parent as before.
6. All work whose failure can make parent start return false, including initial
   diagnostic storage/properties, precedes **either** registration. This also
   applies to the keyboard-only fallback after a T2 preparation failure. In one
   final transport-gated action verify no stop/fault and prepared state, then arm
   the existing2ms timer. If arm fails, leave both registrations absent and routing
   disabled. Outside the gate stop the unpublished prepared child exactly once,
   detach/release it, then balance the successfully started unpublished parent
   with base stop exactly once and dispose its transport. Parent may return false
   only before commit. There are no matched keyboard clients to withdraw on this
   path; this statement depends on both registration and direct-open guards.
7. After successful arm, still under the gate, take exactly one permanent parent
   retain and, if prepared, one permanent child retain, keep childOwned, and set
   committed/published/routing state. These operations must not allocate or fail.
   Outside the gate explicitly register the parent, then the prepared child,
   both with kIOServiceAsynchronous. A T2-preparation failure registers only the
   keyboard parent and preserves its failed T2 status. There is no false-return,
   cleanup rollback, or required fallible allocation after commit. Installing
   the prepared committed-state diagnostic is treated as a runtime operation:
   failure disables the child route and is recorded, never rolled back into a
   failed parent start. Later failure
   is a recorded runtime fault. A stop/fault observed before a registration
   request skips it. A concurrently issued asynchronous request may still match;
   retained state and stopped admission/routing guards must handle that race.
   Neither case releases the cohort or returns false from start.
8. IOHID interface/event-driver matching is asynchronous. Parent start true,
   child registerService returning, or child handleReport returning success is
   not proof that an event driver or SFHID queue is ready. Require the independent
   registry/endpoint gate described below.

No arbitrary delay makes this ordering safe. The timer cannot execute its action
before the gated commit completes. A timer fault immediately afterward inhibits
routing and records a runtime failure; it cannot roll back the retained cohort.
The first-match callback may run before outer start returns because all failure
rollback ended before either registerService call. Registration-requested is
distinct from matched/ready, and a concurrent stop cannot be turned into success.

RegisterService=false suppresses matching, not registry visibility. On the T2
path both handleOpen overrides reject clients before commit and after stop;
the parent's newUserClient refuses precommit/stopping access before invoking
the inherited implementation. The child always refuses raw user-client creation.
No alternate startMatching/registerService path or property-driven publication
is allowed. These admission checks must use a defined atomic phase read without
introducing an arbitration-lock/workloop-gate inversion. Parent registerService
order and its keyboard report filter remain distinct: changing publication timing
does not change the descriptor, report bytes, SPI/power/PIO or enumeration keys.

## Child API and capability limits

The proposed child supplies copied product/vendor/version/country properties,
transport SPI, Built-In and HIDDefaultBehavior through the existing IOHID API.
Vendor/product remain the observed05ac:0341; distinguish interface2 through the
actual provider relationship and a numeric interface property, not a fabricated
product or serial number. Its report descriptor is a fresh owned memory
descriptor containing exactly the acquired110 bytes. Parent descriptor90 remains
unchanged.

The exact child interface key is **SFM1SPIInterfaceID**, an OSNumber created
with value2 and bit width32, installed and read back before child base start.
The getter contract is exactly four bytes `02 00 00 00` on this ARM64 target.
It is not an eight-byte number, OSData alias, string or Boolean. The pinned
`IOUserClient.cpp:3708-3782` property-bytes routine serializes an OSNumber with
numberOfBytes() from its uint64 value, with the target's native byte order.
That getter does not return the OSObject type: the gate verifies bytes/length;
the independently bound producer source verifies that the property is OSNumber.
`ReportDescriptor` remains the base-published OSData, not a second synthetic
property. A bounded read must return exactly110 bytes identical to this boot's
acquired descriptor and its approved digest.

Keep the normal single-interface behavior. The first collection is Mouse,
not Apple's MultipleInterfaces declaration; do not invent a splitting property
or replace the descriptor to create three artificial children.

Explicit synchronous and completion-form getReport/setReport return
kIOReturnUnsupported locally. They do not queue a completion after returning
Unsupported. Property writes cannot become a transport operation. No LED
handler, output callback, feature refresh or provider-open action is installed.
For T2, direct raw user-client creation is Unsupported; ordinary kernel
IOHIDInterface/EventDriver clients remain supported. This also prevents a
pre-registration user client from treating a prepared child as active.

The base IOHIDDevice implementation already returns Unsupported for the
completion-form report operations (`IOHIDDevice.cpp:1739-1760`); explicit child
overrides make the passive contract testable and prevent later accidental
forwarding. Their signatures must be checked against the pinned header when
implementation is separately authorized.

## Exact input routing

Run routing only on complete, CRC-checked DLSP_MESSAGE results from the existing
single SPI reader. Keep device1's existing m1kbd_keyboard_input decision and
keyboard handleReport path unchanged.

A dev2 candidate requires flags0x20, device2, command0x10, a non-null payload and
1..2038 payload bytes. The HID report ID is **payload[0]**. The message's separate
report/subdevice/id/response_length fields are diagnostic only here: the reference
input dispatcher checks device and command, not an equality between those fields
and payload[0] (`spi-hid-apple-core.c:508-527,748-776`). Do not repeat the
earlier reply-ID error or require speculative field equality.

| HID ID | Exact payload bytes, including ID | Permitted handling |
|---|---:|---|
| 0x02 | 8 | Unmodified generic Mouse report: three buttons, signed relative X/Y and four constant padding bytes. |
| 0x3f | 17 | Unmodified vendor fields under a Touch Pad collection; no local interpretation. |
| 0x44 | 1752 | Unmodified vendor array; no local interpretation. |

Require exact length, never minimum length, truncation, padding, prefix
acceptance or a limit derived from advertised2048. Keep the ID byte in the copied
report and submit as kIOHIDReportTypeInput. Full report0x44 is below2038;
fragment/reassembly/message/descriptor capacities do not change.

All other IDs, including0x60, and every mismatched length are consumed but not
submitted. Count the refusal and record a bounded numeric reason/ID/length; no
payload or key bytes. Never synthesize a reset response, button release, zero
pointer motion or dimensions. A long report0x02 is not silently reduced to its
8-byte mouse prefix. An envelope that is not input remains the existing
other-message path.

Only child-ready dev2 messages reach child handleReport. If there is no ready
child, consume/count them without calling the parent keyboard path. Memory
allocation or IOHID submission failure inhibits further child submission and
records a T2 fault; no child recreation/retry. Existing malformed packet/message
CRC or transport failure continues to stop the parent transport.

The eight-message poll budget and2ms rearm remain unchanged. Copy before the
next dlsp_push invalidates the borrowed payload. A non-null gate alone is not a
dispatch-completion certificate: hold references and an in-flight count through
the callback return, including any re-entrant or sleeping consumer.

## Stop, failure and retained ownership

Use separate phase flags for init/prepared, parent/child base-start success,
commit/pin-owned, registration requested, route-ready, route-disabled,
stop-requested, private stop-in-progress and private stop-completed.
Do not overload the existing parent published boolean with all of these states.

**Precommit:** no report source or parent/child registration exists. Stop a successfully
prepared child exactly once, detach it and release its allocation reference before
parent transport disposal. Child base-start failure already self-stops. The
child's handleStop clears childSink by expected-pointer comparison under the gate,
including during self-stop; clearing twice is harmless. Child detach and releases
occur outside the gate. On rollback, move childOwned to a local strong reference
and clear the owned field under the gate after disabling the sink; detach/release
that local outside the gate. No child survives a false-return parent start.
Balance successful parent base start once only after child rollback. Base-start
failure self-stop must set the same private stop state, so outer cleanup cannot
repeat it. Mark private stop-completed only after IOHIDDevice::stop returns:
the base still removes its event source and releases interface objects after
handleStop. The guarded stop wrapper owns this bookkeeping, not the hook alone.

**Postcommit runtime fault:** under the same gate clear readiness and childSink,
stop rearming and retain the cohort. Existing fail() may perform its bounded
m1kbd_hw_stop operation when not busy; this does not authorize closeAfterQuiesce,
unmapping or transport disposal. Do not free a callback, report, child or shared loop from the
poll callback. Registry state must explicitly show faulted/not-ready; service
presence cannot masquerade as working input.

**Independent child termination/stop after commit:** willTerminate disables
the expected child sink before forwarding the notification. The outer stop
wrapper also disables the sink, denies further opens and records
TEARDOWN_DEFERRED. It does **not** call IOHIDDevice::stop, detach/release the
owned child or dispose its HID tables/event source, even if the sampled dispatch
count is zero. Keep the parent-owned child reference until reboot. This applies
both to normal framework callbacks and direct/re-entrant stop. The keyboard may
continue; no child hook closes SPI or releases the parent's loop. Framework
detach may remove the service relationship, but not these explicit retained
objects. Existing in-flight submissions hold their own references until return;
they do not dereference a new getProvider result after detach. No child is
recreated. There is no new child-teardown panic, wait on its own callback, or
claim that superclass cleanup completed.

**Parent stop after commit:** always inhibit both routes and timer rearming,
deny new opens, cancel pending polling as appropriate to the existing timer
contract and record TEARDOWN_DEFERRED. Do not call base stop, quiesce's destructive
transport-close path, disposeTransport, release childOwned or drop the permanent
parent pin. In-flight callback return may release its per-dispatch references
and memory descriptor; it must not trigger deferred resource destruction.
There is no asynchronous teardown worker or retry. Zero dispatches or zero open
clients is not upgraded into a framework/matching completion certificate.

This unconditional committed-retention rule is deliberately narrower than an
unloadable driver. It also protects a first-match callback or handleOpen already
in flight when stop occurs. The outer parent **and child** stop wrappers decide
before any superclass call; guarding only handleStop is too late. The existing
parent stop/free/dispose implementations must not be reused unconditionally.

Required destruction barriers: handleStop and disposeTransport may perform the
existing resource cleanup only in a private, never-committed state. A committed
call records refusal and returns without removing/releasing the shared loop or
HID event sources. free may chain to the base only for never-committed objects.
The permanent parent/child pins and parent-held child reference make committed free
unreachable through balanced reference releases. An unexpected committed free
is an invariant violation: refuse destructive cleanup and record it; do not
resurrect a zero-count object or treat that branch as working reclamation.
Tests must establish that balanced releases never reach it. No callback path
can release either permanent pin. The bounded retained set is one parent, at most
one committed child, their existing event sources/workloop, copied descriptors
and transport allocations; no further allocation cohort is admitted.

The existing parent terminate refusal and kext stop=KERN_FAILURE remain.
This first candidate does not support unloading or reclaiming committed
transport/child objects. Successful close of userspace queue descriptors does
not certify kernel HID callback quiescence. A later unloadable driver needs a
separate completion-proven teardown design; it must not reinterpret
terminate(kIOServiceRequired|kIOServiceSynchronous)==true as that proof.

Root accepts bounded retention in principle; this revision makes its exact
stop/free barriers reviewable by root and Claude. It is not evidence that the
future implementation enforces them. Admission/routing closure is required; resource
reclamation after commit is explicitly unsupported.

## Registry readiness and evidence

Use the concrete class name SFM1SPITrackpad. Incorporate Claude C1/C2:

* Registry must show exactly two IOHIDDevice instances: one SFM1SPIHID and its
  one SFM1SPITrackpad child. The child's provider registry ID must equal that
  parent, interface must be2, and the actual published descriptor bytes/hash must
  match the acquired110-byte record. No SFSerialHID or other HID device class may
  substitute. Root owns the collector and host validation.
  Require bounded property-bytes reads of child `ReportDescriptor` (exact110)
  and `SFM1SPIInterfaceID` (exact4, LE value2). Missing/malformed properties or
  MIG failure after observing a child are query/identity failure, not clean
  child absence. This implements Claude L2; root owns the gate-design update.
* Require both ready character endpoints and the rev002 queue ready kind2
  observation after BSD readiness. The mouse node is shared; node existence or
  a guessed pathname alone cannot attribute it to this native child.
* Inventory must not open either input node. The real seat may open them only
  after registry/readiness checks; closing the seat must restore its existing
  console-grab state. Host input remains absent and VUART remains sealed.
* Record child prepared/registration-requested separately from actual
  interface/event-driver/queue readiness. Numeric counters distinguish dev2
  observed, length-valid, submitted, rejected and failed. A successful
  handleReport return is only submission: IOHIDDevice returns success after
  processing even when no consumer callback is attached.
* Enumeration result0 is preserved as enumeration. It is not overwritten by a
  T2 startup failure. Publish a separate numeric T2 result on the persistent
  provider as well as the parent/child where available; no report payloads.

### Separate T2 state diagnostic

Settle the property name **SFM1TrackpadChildStateV1**, copied OSData with exactly
32 bytes. All fields are fixed-width native little-endian32-bit words on this
ARM64-only candidate; no packed pointers or implicit padding. The future layout
header must assert total size and each offset. It is separate from the frozen
556-byte enumeration record, which remains unchanged.

| Offset | Field | Meaning |
|---:|---|---|
| 0 | version | 1 |
| 4 | byte_size | 32 |
| 8 | committed | Exact Boolean0/1; final non-rollback commit happened. |
| 12 | route_ready | Exact Boolean0/1; the parent sink currently admits dev2 delivery when sampled. |
| 16 | faulted | Exact Boolean0/1; T2 preparation or runtime failure recorded; sticky for this instance. |
| 20 | stop_requested | Exact Boolean0/1; retirement requested; sticky. |
| 24 | last_error | Signed int32;0 on a ready state, otherwise actual failure/refusal reason defined by the implementation's frozen table. |
| 28 | reserved | Must be0. |

Required admission tuple: version1/size32, committed1, route_ready1, faulted0,
stop_requested0, last_error0, reserved0. Other well-formed states are NOT_READY;
missing bytes, bad length/type encoding or impossible flags are diagnostic
failure, never ready. Commit alone is insufficient. A fallback keyboard commit
after child preparation failure has committed1, route_ready0, faulted1 and a
nonzero error. A normal child retirement sets route_ready0/stop_requested1;
it need not invent a runtime error. Enumerated-but-uncommitted state is not ready.

The parent owns state and serializes changes under the existing transport gate.
It publishes independent immutable copies on itself, the child when available,
and its persistent IOResources provider for pre-start/refusal diagnostics. Root's
gate requires the copy on the actual child identified by the provider relation;
another object's copy cannot substitute. Each copy is a state observation, not
an atomic transaction with other copies, registry enumeration, descriptor reads,
endpoint stat, IOHID matching or later seat startup. No liveness lease or input
proof follows, even from a consistent ready tuple.

Prepare the ready-record storage before commit, but do not install a record
claiming committed1 until the actual pins/commit exist. Precommit storage or
initial uncommitted-state publication failure rolls back privately. Installing
the committed record can still fail: treat that as a postcommit T2 runtime fault,
disable the child route, invalidate any partial ready copies and skip child
registration if not already requested. Retain the cohort; never return false
from parent start or claim ready on an IOLog alone. This preserves truthful
commit state without assuming setProperty cannot fail after preallocation.
Runtime fault/stop first disables routing, then invalidates the old ready
property on each owned target before attempting replacement. If allocation or
publication fails, leave that target's property absent and log a bounded numeric
publication error separately from the original T2 cause. Never leave a known
stale ready record installed as a fallback. A getter that already copied the old
record can still finish: that is part of the explicitly non-atomic snapshot scope.
Do not mutate an OSData buffer already visible to MIG readers. No retry thread,
notification-based rematch, resource publication or new request is introduced.

The exact descriptor can support a genuine Mouse usage and hence a genuine
mouse endpoint once IOHIDEventService starts. The vendor collections are not
proof of a native multitouch implementation. No report has yet been observed
under this proposed child.

## Required tests before implementation approval becomes a candidate

These are planned checks, **not tests run by this design review**.

1. Admission: all missing/malformed/zero boot-argument combinations refuse before
   probe claim/hardware; only the closed triple opts in. A T1-only candidate
   creates no child. Exact descriptor match/mismatch/truncation and bad
   observation stage/result/mask refuse without substituting a descriptor.
2. A source-aware lifecycle harness executes the real future start/stop bodies:
   inject every allocation, property, attach, base-start and timer-arm failure.
   Child start must occur after successful parent base start, outside the gate,
   before timer arm. RegisterService=false must be present and checked before
   each device's base start; inject parent and child property-write/readback
   failure. No registration, matched interface, client open or live sink may
   remain after parent false return. Exercise direct registry open before commit.
3. Model the actual IOHID base self-stop on start failure, not a stub that merely
   returns false. Verify one stop, one detach and balanced private references;
   repeated stop/free/dispose must not double-stop or release.
4. Timer failure after child preparation must withdraw both unpublished devices,
   with zero registerService calls. Also test child-preparation failure followed
   by parent timer failure. A first-match callback immediately during parent or
   child registration must see only committed, fully copied state. There is no
   fallible false-return path afterward. Exercise stop between the two requests
   and stop concurrent with a request already issued; no rollback or freed owner.
5. Re-entrant child stop during report dispatch must clear the route and retain
   both owners/report until return. Parent-stop while a consumer yields the gate
   must not treat gate acquisition as completion. Pending dispatch/client/stop
   retains resources and records deferred teardown; no panic or UAF. Repeat at
   zero dispatch count: committed stop still does not invoke either HID base
   stop or disposeTransport. Include keyboard dispatch as well as child dispatch.
6. Child willTerminate/stop before and after commit, repeated stop, failed
   termination initiation, accepted-but-delayed termination and detached retired
   child all leave no active sink. No child path closes shared hardware or
   drops the parent's loop. No implicit retry/new instance. Apply ordinary
   framework/attachment and dispatch releases after stop: both permanent pins
   and childOwned must remain, and neither committed free may be reached.
   Direct handleStop/dispose calls cannot bypass the guard; private cleanup
   still completes and balances all references. Mutants dropping either parent
   staging or a committed destruction barrier must fail the harness.
7. Report tests use genuine full-envelope parsing plus synthetic contents:
   accept only dev2/flags20/command10 with (02,8),(3f,17),(44,1752).
   Reject each length +/-1, unknown ID, missing ID, null/zero/2039 payload.
   Vary diagnostic SPI IDs without treating them as the HID ID. Preserve every
   forwarded byte, retain the ID, and prove dev1 never reaches the child.
8. Feed fragmented1752-byte input through the unchanged native_protocol engine;
   corrupt CRC, overlap/order and over-cap cases must remain failures.
   Overwrite borrowed delivery storage after copying and verify the in-flight
   report stays intact. Keep the8-message callback budget.
9. Call both report-operation overloads and property paths: Unsupported,
   zero SPI requests, no completion queued after refusal. Assert no SET/feature/
   output/haptics/dimensions caller and no new request key.
10. Test that registration/node/counter submission is not classified as pointer
    input. Host fake tests and later SDK linkage cannot replace actual child/
    event-driver/queue/runtime evidence.
11. Interface property: exact OSNumber width32/value2 before child base start;
    reject wire lengths1/8, wrong value, missing property and MIG failure. Require
    actual ReportDescriptor bytes, rejecting every changed byte and length.
    Exercise every T2 state flag/size/version, failure before and after commit,
    and ready-record storage failure before commit. Commit-record installation
    failure must retain the cohort, disable routing and prevent a ready verdict;
    it must never return false or free an already committed object. Runtime diagnostic
    allocation/publication failure removes old ready data while preserving the
    original fault code; a previously copied ready snapshot is explicitly not a
    future-liveness certificate. Verify both persistent refusal and child copies.

Before a future boot require Claude's final lifecycle review, root's SDK/KC and
raw-source binding, the separate exact HOST_CONTRACT and new supervisor tests.
No implementation or hardware is authorized by this document.

