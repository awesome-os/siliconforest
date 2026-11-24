# T2 passive native SPI trackpad child — lifecycle design 001

Status: **DESIGN ONLY; awaiting root and Claude review.** No child code, build,
packet, host change or hardware operation is supplied. Root owns
`HOST_CONTRACT.md` and all future host/SDK/KC work. The frozen T1 producer,
collector, captures, keyboard sources and existing native candidates remain
unchanged.

## Decision and scope

Use one new programmatically created `PDM1SPITrackpad : IOHIDDevice` inside
the existing SPI owner's bundle. Its provider is the actual
`PDM1SPIHID` instance, never IOResources. The parent remains the keyboard's
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

Proposed source opt-in: `pdm1trackpadchild=1`, accepted only together with
`pdm1kbd=1 pdm1trackpadenum=1`, all parsed as the exact value1 before the
existing once-only probe claim. No token, malformed token or missing dependency
must reach T2 creation. Root defines the matching exact host field; this plan
does not alter that contract.

## Grounded constraints

The pinned baseline is PureDarwin
`e7aa00a60f0b56ef35aa24252e81467787bf71ea`.
The composed T1 parent is adapter001
`PDM1SPIHID.cpp 74a22b56...` plus core002. See the separate source/SDK review.

* `IOHIDDevice.cpp:352-378`: base start obtains and retains the workloop,
  installs its event source, calls handleStart, obtains and parses the descriptor.
  `IOService.cpp:1381-1388` inherits the provider's workloop. An attached child
  therefore uses the parent's transportLoop without overriding getWorkLoop.
* `IOHIDDevice.cpp:418-481,499-508,1964-1987`: base start prepares interface
  objects and a first-match notifier, then registers. Setting
  `kIOHIDRegisterServiceKey` false suppresses that final registration.
  The actual PureDarwin property string is **RegisterService**
  (`include/pd_hid_compat.h:58-60`), not an invented HID-prefixed string.
  Interface attach/start occurs later through the first-match callback.
* `IOHIDDevice.cpp:483-487`: a failed base start invokes virtual stop itself.
  A caller must not blindly balance that failure with a second base stop.
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
| Published lifetime | One bounded parent/child cohort for this boot. Parent module unload remains refused. Retired objects are retained until reboot in this first candidate. |

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

1. Perform the existing parent resource setup and `super::start(provider)`.
   This obtains the keyboard and dev2 metadata/descriptors. If it fails, it
   already invokes parent stop; perform only idempotent remaining disposal.
   No child exists and no child callback can be registered.
2. After parent base start succeeds, validate the completed dev2 observation
   and exact110-byte descriptor. Allocate/init the child and copy its descriptor,
   numeric identity and country. Set and verify `RegisterService=false` before
   attach/start. Parent publication remains the existing keyboard behavior;
   only the new child is staged.
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
6. If a child is prepared, all potentially failing work required by parent start
   must precede child publication. In one final transport-gated action, verify
   no stop/fault and prepared state, arm the existing2ms timer, then commit
   published/routing state. If arm fails, leave routing disabled. Outside the gate
   stop the **unpublished** prepared child once, detach/release it, then balance
   the successfully started parent and dispose its transport. Parent may return
   false only on this pre-publication path.
7. On successful commit, retain the cohort for its boot lifetime and install
   childSink/ready under the gate. Outside the gate invoke the child's
   registerService with asynchronous matching. There is no fallible parent-start
   step or false-return branch after this commit/publication point. Later failure
   is a recorded runtime fault, not retroactive failed start.
8. IOHID interface/event-driver matching is asynchronous. Parent start true,
   child registerService returning, or child handleReport returning success is
   not proof that an event driver or PDHID queue is ready. Require the independent
   registry/endpoint gate described below.

No arbitrary delay makes this ordering safe. The timer cannot run before the
gated commit. A timer fault immediately after commit inhibits routing and records
a runtime failure; it must not reclaim the still-published child. The first-match
callback is allowed to run before the outer start returns because all failure
rollback ended before registerService.

## Child API and capability limits

The proposed child supplies copied product/vendor/version/country properties,
transport SPI, Built-In and HIDDefaultBehavior through the existing IOHID API.
Vendor/product remain the observed05ac:0341; distinguish interface2 through the
actual provider relationship and a numeric interface property, not a fabricated
product or serial number. Its report descriptor is a fresh owned memory
descriptor containing exactly the acquired110 bytes. Parent descriptor90 remains
unchanged.

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

Use separate phase flags for init/prepared, child base-start success, commit,
registration requested, route-ready, route-disabled and stop-completed.
Do not overload the existing parent published boolean with all of these states.

**Precommit:** no report source or child registration exists. Stop a successfully
prepared child exactly once, detach it and release its allocation reference before
parent transport disposal. Child base-start failure already self-stops. The
child's handleStop clears childSink by expected-pointer comparison under the gate,
including during self-stop; clearing twice is harmless. Child detach and releases
occur outside the gate. On rollback, move childOwned to a local strong reference
and clear the owned field under the gate after disabling the sink; detach/release
that local outside the gate. No child survives a false-return parent start.

**Postcommit runtime fault:** under the same gate clear readiness and childSink,
stop rearming and retain the cohort. The existing parent's hardware failure/stop
policy is retained. Do not free a callback, report, child or shared loop from the
poll callback. Registry state must explicitly show faulted/not-ready; service
presence cannot masquerade as working input.

**Independent child termination:** permit the normal child IOService termination
path. willTerminate and stop/handleStop first clear its parent's sink under the
gate, then run their superclass behavior. They never close SPI, stop the
keyboard, release the parent's workloop reference or detach the parent.
The parent keeps its allocation reference to the retired child for this boot;
no new child is allocated. This prevents a stale route or a child-stop callback
from destroying its provider. There is no new child-teardown panic path.
Mark child stop-completed only after IOHIDDevice::stop returns, not inside
handleStop: the base still removes its event source and releases interface objects
after that hook. Duplicate stop must not run that base cleanup twice.

**Parent stop after publication:** first disable routes/cancel polling under the
gate. A gate acquisition or cancel request is not alone proof that a report
callback has returned. The committed boot-lifetime retain prevents parent free
while a child/client/dispatch remains. Do not manually stop/detach a published
child with open clients. Framework termination must stop clients before
providers; if a direct/abnormal stop lacks that completion, record
TEARDOWN_DEFERRED and retain the affected HID event sources, shared loop and
objects until reboot. No asynchronous teardown worker or retry is introduced.
No base free or transport disposal may run while dispatches or a child stop can
still use them. This retention is deliberate, bounded to one cohort and reported;
it is not a claim of successful reclamation.
The outer parent stop must make this decision before its current
super::stop call (adapter line396); putting the decision only in handleStop is
too late to prevent the superclass from removing the parent's HID event source.

The existing parent terminate refusal and kext stop=KERN_FAILURE remain.
This first candidate does not support unloading or reclaiming committed
transport/child objects. Successful close of userspace queue descriptors does
not certify kernel HID callback quiescence. A later unloadable driver needs a
separate completion-proven teardown design; it must not reinterpret
terminate(kIOServiceRequired|kIOServiceSynchronous)==true as that proof.

Root/Claude must explicitly review this boot-lifetime-retention choice. It
keeps the first child design bounded and avoids introducing a second asynchronous
lifecycle solely to reclaim one experimental object.

## Registry readiness and evidence

Use the concrete class name PDM1SPITrackpad. Incorporate Claude C1/C2:

* Registry must show exactly two IOHIDDevice instances: one PDM1SPIHID and its
  one PDM1SPITrackpad child. The child's provider registry ID must equal that
  parent, interface must be2, and the actual published descriptor bytes/hash must
  match the acquired110-byte record. No PDSerialHID or other HID device class may
  substitute. Root owns the collector and host validation.
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
   before timer arm. RegisterService=false must be present before child start.
   No registration or live sink may remain after parent false return.
3. Model the actual IOHID base self-stop on start failure, not a stub that merely
   returns false. Verify one stop, one detach and balanced private references;
   repeated stop/free/dispose must not double-stop or release.
4. Timer failure after child preparation must withdraw the unpublished child.
   A first-match callback immediately during registration must see only
   committed, fully copied state. There is no fallible false-return path afterward.
5. Re-entrant child stop during report dispatch must clear the route and retain
   both owners/report until return. Parent-stop while a consumer yields the gate
   must not treat gate acquisition as completion. Pending dispatch/client/stop
   retains resources and records deferred teardown; no panic or UAF.
6. Child willTerminate/stop before and after commit, repeated stop, failed
   termination initiation, accepted-but-delayed termination and detached retired
   child all leave no active sink. No child path closes shared hardware or
   drops the parent's loop. No implicit retry/new instance.
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

Before a future boot require Claude's final lifecycle review, root's SDK/KC and
raw-source binding, the separate exact HOST_CONTRACT and new supervisor tests.
No implementation or hardware is authorized by this document.

