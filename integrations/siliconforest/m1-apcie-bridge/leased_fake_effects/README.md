# LEASED_FAKE_EFFECTS, revision 001

This new host-only fixture implements only the composition contract in
`reports/sprint-010/bcm4378-owned-composition-design-001.json`. It directly links
unchanged `private_lifecycle/lifecycle.c` from receipt **c585fd8b** and the frozen
original fake transport, identity/name, DART, IRQ, ring and opaque-item models.
There is exactly one actual `bpl_registry`, a `bpl_lease` and `bpl_ticket`.
There is no Boolean owner, second sidecar registry or dependency on the earlier
standalone fake ownership/effect-ledger layers.

The fixture does not use, import or expose native79 descriptors, manager jobs,
tokens or embedded leases. It does not integrate `control_protection`, call
pmap or cast tokens/numeric labels into pointers. Native-backed exposure remains
blocked by the missing public coordinator/backing-view interface described in
the design. These tests do not run IOKit against stand-in objects.

Three distinct `_Alignas(16384)` ordinary host pages live outside the stable
control context: root, leaf and data, each exactly 16K. The control context has
inactive codec scratch and the fake register file; those are not extra claimed
backing. The data page supplies only its 32-byte prefix to the depth4/item8 ring;
its claimed/mapped span remains the full16K. Pointer-to-number conversion copies
the real host CPU addresses into metadata; there is no inverse conversion.
The PA/IOVA numbers are the **previously pinned synthetic fixture labels**, never
physical discovery, native allocation, DMA addresses or hardware permission.

The sole core registry gets explicit synthetic domains, RAM/reservation policy,
RID0x100/SID3/vector bitmap1 and exact root/leaf/data metadata before any model
call, including the endpoint configuration selector. This fixture uses handles
1/2/3 and its lease generation as each span's generation; that equality is an
explicit fixture invariant, not an extra rule attributed to the frozen core.
Every read/write callback checks the real registry/slot/generation/request/spans.
Callbacks during a mutating call also require its exact live PENDING core ticket
and resource mask. Trace fields record the current operation, not ownership.
Duplicate RID, SID or vector claims and stale/wrong-registry identities refuse
in the actual core without any fake callback.

`MUTATE(kind, resources, expression)` calls `bpl_effect_begin` before evaluating
the model expression and calls `bpl_effect_result` exactly once afterward.
Only zero becomes REPORTED_SUCCESS; other model results become REPORTED_FAILURE
with the original error. Lost-result injection records UNKNOWN/nonzero and
retains ownership. Pure read failures after prior history also quarantine.
Pure initial preflight failure has no history and can retire only its private
metadata; it does not free the three static pages. No rollback/retry clears an
effect to recover private retirement.

The exact bounded healthy chain uses **18 effects**, precomputed against32:

| Calls | Kind | Resources | Count |
|---|---|---:|---:|
| transport init, identify, selector, shared metadata | CONFIG | 8 | 4 |
| fake map init and page commit | MAPPING | 7 | 2 |
| IRQ open, arm, unmask, receive, dispatch, mask, close | INTERRUPT | 16 | 7 |
| ring open, descriptor configure, logical close | DESCRIPTOR | 12 | 3 |
| frozen `bfi_submit` composite operation | DOORBELL | 12 | 1 |
| explicit fake consumer-index test stimulus | PRODUCER | 12 | 1 |

Even model control initialization is conservatively journaled. Read-only
identity, firmware-name construction, translation and `bfi_check` need no false
effect record. No firmware request/download or image bytes are involved.
Submission uses the frozen opaque bytes `11 22 33 44 55 66 77 88` and the unchanged
`bfi_check`/`bfi_submit`, not an invented firmware msgbuf format. One submission
ticket covers all possible copy/index/bell effects; separate fake callback
counters distinguish attempts, applied index and bell. They are not three core
entries, and an attempted operation is not proof its bell happened.

Advancing the fake consumer index is an explicitly journaled ordinary-RAM test
stimulus, not firmware activity or DMA completion. A masked pending IRQ prevents
logical close. After both logical closes succeed, `bpl_retire_private` must
still return RETAINED; all spans, claims, fake descriptors and context stay live.
Failures and unknowns quarantine and retain requester, vector and backing.
Private retirement after even a successful/no-op ticket is forbidden by c585.
Independent test-case reconstruction is isolation, not a recovery API.

Run `python3 integrations/puredarwin/m1-apcie-bridge/leased_fake_effects/test_fixture.py`.
The suite pins c585, uses actual original model implementations, checks callback
ordering, all before/after-write fault points, unknown results, partial index
publication, logical-close retention, generations, exclusive claims and journal
exhaustion. Counted mutants must compile then fail assertions. ASan/UBSan apply;
LSan is disabled for the known ptrace restriction.

`LEASED_FAKE_EFFECTS` and all explicit model/accessor fake flags are required.
Default and host-compiled KERNEL entry stubs return GATED and never run the
fixture callbacks or construct a registry/backing owner. Fake+KERNEL,
shared-PHY/PMGR writer opt-in and NDEBUG are compile-refused. Existing GPIO152
and SMC paths remain compiled out. No hardware, device, physical mapping,
firmware, native backing join, DART/MSI programming, kext/KC, packet or activation
is supplied. The actual saved hardware observation remains PHY_NOT_READY.
