# Private BCM4378 lifecycle metadata

This original pure C core replaces the integration test's single Boolean owner
with bounded metadata reservations and an effect journal. It does not implement
a driver or call the existing fake transport/DART/IRQ/ring models. It performs
no allocation, provider callback, firmware access, MMIO, mapping or device work.
There is no ACTIVE state, physical drain acknowledgement, unmap or exposed
release API. The hardware STOP and compiled-out shared PHY/PMGR writes remain.

The design input is `reports/sprint-010/bcm4378-real-lifecycle-gap-001.json`
(SHA256 `483b107fce508d3d58d0b38c3a49041a08d7b42627b7a905bf1fa118d2d75e39`).
It identifies obligations absent from the current models. This leaf implements
only private metadata admission and conservative bookkeeping, not those missing
providers. No external driver source was copied or fetched; original license
selection remains pending.

Numerical page, PA and IOVA bounds follow the unchanged original
`integrations/puredarwin/m1-dart/dart_codec.h` (SHA256
`78c4818292297852d4d4df410a7286651f6d4d2e2cfaa9c2dbc1ff6a2a1b31b0`).
This core does not link that codec or validate a page-table graph. The16KiB
three-span bound and minimum effect masks are conservative new metadata policy,
not new hardware-format claims.

## Ownership and lifetime

The trusted platform caller supplies one stable, initially all-zero registry
per resource domain and serializes every access for its entire lifetime. The
registry copies its bounded policy. Four leases may reserve distinct RID labels,
SID values and vector sets. A conflict with a live or quarantined entry fails
without changing the registry or output. RID values `0x100..0xffff`, SID values
`0..15`, and vector bits `0..7` are accepted metadata bounds, not discoveries of
usable hardware resources. Domain identifiers are trusted labels, not authority.

Every lease identifies the exact registry address, slot and nonzero64-bit
generation. Retirement invalidates that lease before its claims become reusable.
The last generation may be issued once; exhaustion is permanent even after
retirement. Reinitialization of a nonzero registry fails. Tokens may be copied
only as references to the same lease. A wrong registry/stale lease cannot mutate
another owner's state. Multiple registries do not enforce physical exclusivity;
the platform must prevent duplicate registries for the same real domain.

Visible structure fields permit caller-owned storage; they are not a mutation
API. Changing, copying, zeroing, moving or concurrently accessing an initialized
registry violates the contract. These checks are not a security boundary against
arbitrary privileged writes. The core invokes no callbacks, so there is no
callback reentry path; synchronization is a caller obligation, not an internal
lock. Registry, policy and scratch lifetime remain the caller's responsibility.

## Numeric backing metadata

Each lease may bind one exact root/leaf/data triple, in that order, once. Each
span is16KiB. Nonzero handle and generation labels, aligned CPU/PA spans,
conservative36-bit physical bounds, declared RAM membership and reserved-span
exclusion are checked before commit. Table entries carry no data permissions or
IOVA. Data supports explicit READ/WRITE only and a16KiB-aligned IOVA in
`[0x00100000,0xbff00000)`; this also excludes the MSI page at `0xffffc000`.
Unknown/execute bits are refused. Only explicit values are accepted.

CPU and PA overlap, duplicate handle labels, and data IOVA overlap are refused
within and across all live/quarantined leases. Numeric CPU spans may not cover
the registry itself. This first policy conservatively forbids shared IOVA labels
even across distinct leased SIDs. It also forbids multiple spans from one handle.
The checks do not establish accessible pointers, actual backing, a preparation
pair, bus ownership, cache coherence or physical mapping. Numeric CPU values are
never dereferenced. Declared RAM/reservations remain unverified caller metadata.
Binding copies these descriptions; subsequent changes to input arrays do not
change the entry. A real provider must independently attest them in future work.

## Effect and failure contract

The states are CLAIMED, PRIVATE_METADATA, EFFECT_RECORDED, QUARANTINED and
RETIRED. PRIVATE_METADATA is not hardware readiness. A lease has a32-entry
append-only journal. `bpl_effect_begin` commits its sequence, pending entry and
monotonic resource mask before returning an operation ticket. The caller must
call begin before any future external effect; this core cannot enforce behavior
outside its API, and the ticket is not permission to touch hardware.

Kinds distinguish config, mapping, descriptor, producer, doorbell and interrupt
effects. Their conservative minimum resource masks are fixed in the header.
Table/data effects require bound metadata. Configuration/interrupt effects may
be recorded before binding; after any effect no binding changes are allowed.
Only one result may be outstanding, and ledger exhaustion refuses new effects.

`bpl_effect_result` records REPORTED_SUCCESS, REPORTED_FAILURE or UNKNOWN. These
are caller reports, not observations made by the core. Success clears only the
pending marker; it never clears history or makes the claim privately retireable.
Failure/unknown results latch quarantine and the first nonzero error. Wrong or
replayed sequence for a current lease also quarantines it; wrong/stale lease
identity is refused without modifying somebody else's entry. Invalid arguments
leave the pending obligation intact. Explicit quarantine is sticky, including
when no effect was recorded, and retains every claim and backing description.

`bpl_retire_private` accepts only never-exposed, nonquarantined CLAIMED or
PRIVATE_METADATA entries. It releases metadata reservations only. It does not
free, complete, unmap or authenticate actual storage. A caller with real backing
has separate obligations that this API neither represents nor discharges.
Any recorded effect, even a reported successful no-op, permanently excludes
this retirement path. There is no drain Boolean or forced cleanup escape.

## Validation and limits

Run `python3 -B integrations/puredarwin/m1-apcie-bridge/private_lifecycle/test_lifecycle.py`.
The runner builds the actual core under ASan/UBSan in ordinary host and
host-compiled KERNEL modes, compiles core mutations, refuses assertion-disabled
tests and demonstrates that nonexistent activation/drain/unmap/release APIs
cannot compile. KERNEL mode is the same pure metadata implementation, not a
kernel provider build. No existing fake-model implementation is modified.

Tests reset ordinary fixtures between independent test cases and directly seed
the generation counter to exercise exhaustion. Neither operation is a supported
recovery API. Negative mutations are compiled implementations, not text-only
tests. Leak detection is disabled; the core does not allocate. Frame and import
measurements are reported separately, and cross-compiled ELF is not Darwin SDK
or target execution evidence. Real deployment, backing and teardown remain
blocked by the source design's unresolved hardware/provider contracts.
