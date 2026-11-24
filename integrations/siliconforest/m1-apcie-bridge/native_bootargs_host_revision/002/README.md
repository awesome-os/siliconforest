# Isolated native BootArgs host revision 002

This directory is a source-only before/after proposal, not an applied overlay or
boot candidate. Canonical host0cf9ccc9 and controllercce9803f remain unchanged.
The frozen codec67b0 and independently reviewed ABI verifier revision002 (03df,
profilec4ab) are copied without edits. Blocked verifier001 is not used.

The only build-record opt-in is the exact typed object in `experiment.json`:
`native_bootargs_wire_experiment = {version:1, wire_profile:
"darwin-xnu-e7-arm64-bootargs1152-r2v2-v1", source_revision:2,
source_version:2}`. No caller-provided ABI-success assertion is accepted. Missing
field stays on the old path; malformed presence, duplicate JSON keys or source
hash drift refuses before reboot verification and before worker USB enumeration.
Other existing input/mode gates remain in force and unchanged.

The gate hashes the exact selected KC bytes through the production verifier,
including its fixed UUID, VM layout and entire executable segment/text hashes.
Helpers are bounded, SHA-pinned and executed by `compile()` of the exact bytes
just read and hashed. The verifier rechecks its sibling profile hash on each
call. Controller verification/snapshots include all three new dependencies only
for opted-in runs, with another hash check immediately before each snapshot.
The verifier matches compiled code, not boot/runtime safety; every new candidate
still needs a fresh concrete packet/ABI binding and independent admission.

Only `load_payloads`' final BootArgs write branches. The legacy branch retains
its exact format lookup/write; the native branch copies trusted fields from
`hv.tba`, normalizes integer/Construct values only after type/range checks, and
preserves source R2/V2, video, flags and actual memory size. Observed source R2/V2
is checked after collection, before HV initialization/payload work. No numeric
revision switch, m1n1 module, kernel or bootloader change occurs.

The native writer requires the existing `/chosen/memory-map/BootArgs` pair to be
exactly the predicted address and16KiB allocation, aligned, inside the existing
guest envelope/payload reservation, below the relocated ADT and disjoint from the
existing bounded MCC exclusions. It checks the whole allocation, not just1152
payload bytes. This is not a new complete carveout/RAM ownership claim. The
planner's allocation-review SHA binds a documented source contract; it is not
hardware approval. Per-boot checked bounds/exclusion digest and ABI/source
provenance are recorded separately.

There is exactly one opted-in **final** BootArgs write of1152 bytes and one exact
1152-byte readback; the pinned HV's earlier r2 write remains. Short, corrupt,
mutable or changed-field/allocation/provenance results refuse without retry and
before `hv.start`. Raw readback is private; success additionally saves private
`guest-bootargs.bin`, `guest-bootargs-record.json` and `before-start` payload
metadata. No raw captured device data is placed in these synthetic tests.

Pinned HV18da `start()` serializes the final ADT only; it does not serialize or
rewrite BootArgs. The already-existing `load_raw` r2 serialization happens first
and limits this entire integrated path to607 ASCII command bytes plus NUL,
even though the portable native codec supports1023. This guard is explicit;
there is no expanded command-line capacity claim. Final guest wire bytes still
must be captured on any separately admitted future attempt. No prior runtime
wrong-value, crash or grade claim follows from this source change.

`test_native_host.py` uses the actual selected compiled KC as read-only input,
in-memory corrupt/shadow variants, explicit fake backend objects, and the
extracted real before/after writer functions. Its tests cover missing/malformed
opt-ins, duplicate keys, actual ABI checking, same-hashed-byte execution, drift,
source integer normalization, unchanged legacy writes, native tail oracle,
allocation/envelope/MCC guards, changed values and short/corrupt readback.
Neither that suite nor the relevant existing host/controller suites accesses an
actual proxy, USB device or machine. The tests also bind the final worker call
and prove the gate precedes USB enumeration.

Run `validate.py` to save fresh offline logs. `host.patch` replays only the two
changed host/controller files against the exact before tree; dependencies are
identical in before/after. `manifest.json` and source validation bind the whole
isolated trees, patch, tests and dependencies. No application, M5 staging,
reboot, Signal message or hardware permission is supplied.

Revision002 preserves revision001 independently and corrects its narrow local
consistency blocker (review a8a437c1). It captures normalized guest_base,
bootargs_off, their checked sum, ADT destination and entry before the final
write. ADT destination must equal the relocated ADT address. Every value is
rechecked after readback, before publishing successful evidence. An opt-in-only
worker wrapper copies this saved state and checks it immediately before calling
the original HV.start; later drift refuses without calling that start or retrying.
The legacy branch and ProgressCapture class remain unchanged.

The earlier seven-word start guard already rejects changed start arguments, so
this corrects a local receipt consistency failure; it does not demonstrate or
repair a historical wrong jump. Original HV.start uploads ADT before that guard.
The wrapper proves equality at its call boundary, not immutable globals throughout
the original HV.start. Entry equality is an extra consumed-argument check, not
BootArgs content authentication. No writer or target is executed by these tests.

The original152 passing tests remain. Five focused groups add readback-time
component/ADT/entry/sum-preserving drift, prewrite ADT mismatch, post-writer drift
refusal, captured-state copying and type/range checks. Seven executable Python
mutants must compile and be killed by assertions, with zero error-only kills.
