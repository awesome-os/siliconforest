# Passive trackpad child: proposed host contract

This is preparation for a separate candidate, not an applied host revision or
hardware authorization. The current M1 is in verified Linux. Preserve the
enumeration-only packets, their opt-in and their original captures.

The actual 110-byte descriptor declares Input reports 0x02, 0x3f and 0x44,
with report-ID-inclusive lengths 8, 17 and 1752. It has no Feature or Output
items. The existing descriptor-based multitouch mode-write gate remains closed.
A passive child must not add feature, output, dimensions or haptic requests.

## Admission

A future exact, typed experiment field must distinguish passive child startup
from enumeration-only startup. Its final child class name and schema depend
on the independently reviewed lifecycle design. Do not implement a permissive
placeholder or infer runtime publication from a string or fileset entry.

The candidate must bind the actual new SPI bundle and child implementation,
the captured descriptor and a matching collector. It must keep the native-only
collection, forbid SFSerialHID and SFDARTSurvey, and reject host input injection,
virtual/terminal/keyboard test opt-ins and conflicting experiment fields.
Unknown fields, duplicate JSON keys, incorrect types and malformed mode values
must fail before reboot and in the worker. All previous packet gates must
retain their previous behavior.

Make the new field presence-exclusive with `trackpad_enumeration_experiment`,
`native_keyboard_desktop_experiment` and every other experiment field, including
when a conflicting field's value is null. Its final schema must explicitly
require `feature_writes=false` and `host_hid_injection=false` as booleans, a
passive-child mode, interface2 and the reviewed child class. Preserve the
keyboard-only mode as its existing separate opt-in; use a shared sealed-native
writer path internally rather than calling the Both seat keyboard-only.

The reviewed child design must settle its explicit boot argument and dependency
on the existing keyboard/trackpad enumeration arguments. No child boot argument
may be added for an enumeration-only or ordinary native packet.

## Writer and supervisor

Reuse the reviewed native VUART sequence: optional shell entry, split-nonce
challenge, one fixed launcher, then a sealed writer. Seal before writing the
launcher. No HID scheduler, post-launcher typing or synthesized input.

The new finite supervisor must require the actual child service, its native SPI
parent and both ready character endpoints before opening them. An unrelated
USB mouse or an old SFSerialHID endpoint cannot substitute for the child.
Missing readiness means exit77 before the compositor, with no human prompt.
The refusal must identify a missing child, parent or endpoint separately from
query/identity errors. The registry gate is specified separately in
`REGISTRY_GATE_DESIGN.md`; the original keyboard inventory's convenience
iterator is not strict enough for this admission decision.

Use the real compositor's existing Both seat only after these checks. Keep the
current T1 supervisor unchanged: its deliberate mouse-node refusal is correct
for that experiment. The startup observer, title and receipts must continue
to say input is unverified. Endpoint and seat startup are not physical typing
or pointer evidence.

Preserve finite app cleanup, six closed integrity-checked terminal records,
owned-shell reap and raw wait status, compositor close, text-console return
and mandatory new verified Linux recovery. Keep the established180-second
host bound unless measured evidence requires a separately reviewed change.

## Before a test

Require independent child lifecycle/report-ownership review, actual SDK/KC and
RAM bindings, the final exact host contract and policy tests, a closed-script
review and hash verification on both hosts. Preserve source snapshots, the
TX ledger and raw failures. A future success may establish passive child/seat
startup only; input and physical-panel claims need their own evidence.

That startup evidence must include the actual boot-argument triple, the exact
same-boot acquired descriptor and its digest, raw child diagnostic, registry
class/provider relations, both stat-only endpoints and queue-readiness log,
the sealed TX ledger, zero PTY TX for the unattended startup test, closed
cleanup records, returned-console pixels and verified Linux recovery. Record
`physical_input_proven=false` and `physical_panel_observed=false`. The556-byte
enumeration record alone is not a child-state or registry-provenance record.
Unsupported report operations and the reviewed kernel call paths establish
the passive source contract; an in-band false flag alone is not that evidence.
