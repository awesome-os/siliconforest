# Native keyboard endpoint publication candidate

Unapplied source patch; no SDK/kext build or hardware result. Root owns
`build*.nix`, SDK/KC staging and execution. Native PDM1SPIHID revision005,
PIO/power/protocol code, qualified trees and old binaries are unchanged.

## Exact application

`iohid-publication.patch` changes exact e7aa00a's `PDHIDEventQueue.cpp/.h`.
`iohid-attachment.patch` changes that pin's `IOHIDEventService.cpp`.
`candidate.patch` concatenates both. Apply the combined patch **or** the split
pair at the PureDarwin root. `before/` and `after/` include these three files and
an identical native005 `PDM1SPIHID.cpp` reference. `manifest.json` binds all bytes.
No native-driver overlay patch is necessary.

The common IOHID event service is the attachment boundary: its start has opened
and parsed the interface, parsed supported elements/collection usages and set
`_readyForInputReports=true` before requesting publication. The selector accepts
only Generic Desktop keyboard/keypad or mouse collections. The real J293
keyboard descriptor requests only the keyboard queue. PDSerialHID's parsed
keyboard and mouse collections request their actual virtual endpoints, keeping
that path functional without a synthetic key or motion. Digitizers, malformed
usage pairs and unready services cannot fabricate a mouse endpoint. A mouse
endpoint from PDSerialHID is not a native trackpad. The physical trackpad remains
unsupported; device2 transport/report handling is unchanged.

## Publication ownership and failure

One CAS-installed, boot-lifetime thread call owns all major/node/alias creation.
`THREAD_CALL_OPTIONS_ONCE` prevents overlapping callbacks, including re-pends
while executing (pinned XNU `osfmk/kern/thread_call.h:77`, implemented by the
reschedule path in `thread_call.c`). Callers set monotonic request bits and wake
that owner. No lock is held across cdevsw/devfs calls, and reentry can only
request another invocation. This is the serialization mechanism instead of
holding a mutex across callbacks.

There is no IOBSD notifier: removing that second readiness source removes its
synchronous-registration/removal race. The worker checks `devfs_is_ready` and
uses one delayed callback, at one-second intervals. Repeated same-capability
requests cannot accelerate retries. Each queue has a maximum of 60 publication
attempts; exhaustion is terminal for that queue for this boot. A newly requested
capability can wake the owner earlier. Numeric attempt/last-error state records
storage(1), cdevsw(2), devfs-not-ready(3), node creation(4), or success(0).
These are local failure categories, not fabricated devfs errno values. The
first deferral, successful publication and terminal refusal are logged. A
permanent collision returning NULL cannot loop forever. No filesystem node is
created except through `devfs_make_node`.

The publication call itself can fail allocation before any worker exists. That
failure is counted and logged once; the endpoint stays absent and the request
remains for a later explicit startup request. There is no invented successful
return or automatic callback without an allocated call. The public void API
remains a request, so guest startup must use a bounded real-node check.

CAS installation plus acquire loads protects keyboard, LED and mouse locks and
the console callback. Losing uninstalled allocations are freed. Successful
storage setup and an actual producer's startup request precede the queue-ready
release barrier; only then may devfs expose the node. Open/read check that
barrier. The visible endpoint therefore has its lock, static zero-initialized
ring and (keyboard) console/LED storage ready. Queue/call storage remains valid
until reboot; there is no new unload, producer-detach or reclamation guarantee.

Event push paths never allocate, register, create nodes or schedule publication.
Before storage readiness they only increment an early-drop counter; afterward
real events enter the existing ring. Keyboard sequence assignment is moved into
the same queue-lock section as the slot write, matching the mouse path. No
publication path calls Push/handleReport or changes console bytes. Empty reads
return EAGAIN; opening the keyboard takes the existing global console grab and
last close restores it. Grab state uses acquire/release operations.

## Review boundaries

This implements P1-P6 of the early design review with one owner rather than an
IOBSD notifier plus retry. P7 is documented in the queue header: LED callbacks
still execute under the shared LED lock and must not re-enter registration or
acquire a lock that can do so. Existing callbacks can block; no native SPI LED
handler is added. P8: existing 0666 node modes are preserved for compatibility
with this single-user RAM experiment; this is not a production permission model.
P9: endpoints, open and seat creation are never physical-input evidence.

Shared keyboard records still lack producer IDs and aggregate modifiers across
keyboards. The sequence race is fixed, but multi-keyboard modifier ownership is
not redesigned. Keep host HID sends off for native-input evidence and preserve
the separate host-origin terminal qualification path. No person or fabricated
input is needed to qualify empty-node startup.

## Host validation and next steps

```sh
python3 integrations/puredarwin/m1-native-input/test_publication.py
python3 integrations/puredarwin/m1-native-input/verify_candidate.py
```

The first compiles the complete patched queue, the actual capability-selector
body and the unchanged native start body against host collaborators using
C++14/pthreads, strict warnings and UBSan. Tests cover empty queues, native and
serial capabilities/refusal, concurrent and reentrant publication, CAS setup
races, deferred devfs, allocation/major/node failures, retry exhaustion,
early-report drops, actual queued input before devfs, keyboard sequence order,
start refusal and console grab restoration. Six compiling mutations must fail.
The fake scheduler models the pinned ONCE contract; this is not kernel scheduler,
IOHID descriptor-parser, SDK ABI or hardware proof.

The second checks owned hashes, split and combined patch replay into temporary
copies, and available local pinned sources/frozen native005 files. It does not
apply anything to a live checkout. Root must compile the new IOHIDFamily and
inspect real imports and KC selection. `thread_call_allocate_with_options` is
in pinned `config/Mach.exports:55`; no weak-link workaround is permitted.
The existing native005 bundle can stay immutable because its source is unchanged.

Grok's keyboard-only userspace seat and root's independent native-driver opt-in
remain separate work. The current compositor requires both nodes and the
current terminal-input opt-in intentionally excludes PDM1SPIHID. Keep defaults
off; require the real native member and opt-in for a new packet. Hardware and
physical typing remain unqualified for this patch.
