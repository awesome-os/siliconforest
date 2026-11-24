# Generation-checked fake effect ledger, revision 001

This original host-only slice wraps the frozen ownership registry. It does not
implement a native ownership plan, hardware mapper, MSI controller or a way to
convert a fake lease into a real capability. No new reference was fetched or
copied. Existing registry, DART, interrupt, accessor and codec files stay intact.

Use one zero-initialized canonical ledger per fake endpoint and serialize every
participant. Registry/leases/backing/workspace/policy arrays must stay live,
disjoint and stable. Do not move/copy/edit active state or bypass this layer
with the older APIs. This is cooperative C metadata, not a security boundary
against forged pointers or arbitrary memory writes, and not a concurrent kernel
adapter. Reentrant API calls return BUSY without callbacks. No API allocates or
frees memory; all backing is ordinary caller-owned host RAM.

A token is a slot and nonzero 64-bit generation, not a pointer or physical
capability. Acquisition claims the endpoint's exact RID/explicit SID binding
and one vector together. It validates through private copies of the unchanged
registry before committing either claim. Validation uses read-only fake guard
callbacks, with no owned resource effect yet. Failed admission leaves the output
token, backing, ledger and registry unchanged. After admission an ACQUIRE pair
is journaled and completed with no callback or fallible step after commit.

Every actual later operation begins an append-only PENDING record before its
effect/helper callbacks. The record has generation, slot, sequence, kind, phase
and exact result. PRIVATE_MAP and PRIVATE_RETIRE increment their started count
before callbacks and complete the pair only after success. Read-only preflight
sampling precedes the intent; it cannot perform an effect. During recorded
effects, even later fake register-read callbacks can inspect the PENDING record.
The test backend does so before each read and attempts refused reentry.

EXPOSE sets the sticky exposed bit **before** invoking the synchronous fake
producer. A successful callback does not authorize release. Failed, uncertain
or unrecognized callback results quarantine the lease; failed postchecks also
record UNKNOWN and quarantine. Failed private effects are journaled and
quarantine too. Requester binding, vector, backing and original failure remain
retained. Later attempts cannot retry the quarantined lease as success. This
callback is only an ordinary fake-host effect; no firmware image, DMA, native
device call, actual exposure or actual completion is provided.

`bfl_retire(ledger, token, workspace)` has no success flag. There is no caller
completion method, drain acknowledgement, force-reset, fault clear or exposed
retirement method. It rejects stale generation, exposure, quarantine and any
incomplete private pairs. It stages interrupt close and software map close
together, then checks readiness again before releasing either live claim.
If close1 succeeds privately and close2 fails, both live owners and backing are
still retained and the lease is quarantined. Healthy never-exposed retirement
zeros owned mapped fake table bytes, drops both claims and marks the lease
retired, with no later callback. It is not a physical invalidation/drain/free.
Unchanged interrupt close still refuses pending or unmasked sources.

The two lease slots can be reused only after successful never-exposed
retirement, with a strictly new generation. The 32-record history is never
overwritten, including records from retired generations. Generation wrap and
history exhaustion refuse without effects; exhaustion never forces release.
An exposed or quarantined lease keeps the exclusive requester, so a fresh
generation cannot bypass retention. A callback that does not return leaves
the PENDING/exposed/busy state retained; no nonlocal-exit or cancellation-safe
cleanup is claimed.

All five APIs refuse initial fake link-down, PHY reset or missing clock ACK
without observable ledger, registry, table, workspace or output changes, and
with zero register writes. A transient busy flag prevents reentry during
read-only preflight and is restored on refusal. After an initially healthy
operation starts, private staging can change; a failed/uncertain effect must
instead retain its recorded quarantine fact. Caller serialization is required
between the final sampled guard and CPU commit; this is not a physical atomic
check against real hardware changing asynchronously.

Working host builds require BFL/BFO/BFD/BMI fake flags and the explicit fake
accessor. Default and host-compiled KERNEL return GATED with no callback,
MMIO, ledger or output changes. KERNEL+fake and shared PHY/PMGR writer opt-ins
compile-fail. GPIO 152 and the SMC radio command remain compiled out. There are
no DART/MSI register operations, P_PCIE_INIT, device opens, mappings, kext/KC
links, real allocations or boot packets.

Run `python3 integrations/puredarwin/m1-apcie-bridge/test_bcm4378_fake_effect_ledger.py`.
Tests use the exact frozen IRQ fake register backend, with its old main never
run. ASan/UBSan, intent observation, callback reentry, generation reuse/history,
private-pair completion, all callback outcomes, guard/fault injection and
compiled assertion mutants qualify only this software model. The actual saved
observation remains PHY_NOT_READY, with no real chip/BAR/DMA/firmware-load claim.
