# Complete sampled TZ metadata, not a hardware policy

This original pure C component addresses one specific provenance gap: the pinned
m1n1 compact MCC range list can omit an enabled slot after reporting bad bounds.
That list is not enough to reconstruct the status of every slot. This decoder
requires two complete samples of all four T8103 planes and all four TZ slots.
It neither acquires samples nor approves a native RAM policy.

Numerical facts come from the MIT-licensed pinned m1n1 `src/mcc.c` at
`121fcbf35d4501814b58ca5347926a4173b6a512` (SHA in SOURCE_FACTS.json).
The register map is four planes at stride 0x40000, four slots at stride 0x10,
start/end/enable offsets 0x6a0/0x6a4/0x6a8. The profile concerns MCC0 on T8103.
It is not a format definition for other SoCs, all controllers or firmware holes.
No upstream driver implementation is copied here.

`mcp_plan_read` returns one of 96 **relative offsets**, ordered by pass, plane,
slot and field. Even index zero requires the supplied mapping extent to cover
the entire plan. There is no base address, backend, callback or register access.
The caller must separately establish the actual board, mapping provenance,
access safety and acquisition order. Generating a plan does not authorize it.

`mcp_decode` consumes exactly 384 little-endian bytes: two passes of 48 u32
values. Every byte must agree across passes, including disabled-slot words.
All planes must also agree exactly for this profile. These extra observations
are requirements of a future acquisition, not evidence already obtained from
hardware. The source helper's plane-equivalence assumption is not treated as
proof; this decoder checks the supplied samples.

Enable means any nonzero u32. Enabled start zero, equal/reversed bounds,
overflow, or a decoded span outside the supplied DRAM bounds refuses the whole
snapshot. No bad enabled slot is silently skipped. A disabled slot preserves
its raw metadata but contributes no range. Address conversion widens first,
uses 4KiB units and an inclusive end, then ORs the DRAM prefix exactly as the
pinned source does. Already-prefixed register values are valid when their
decoded span is valid; the decoder does not invent an offset-only convention.
T8103's supplied DRAM base must be 0x800000000 and its extent must be nonempty,
4KiB-aligned and contained below the 36-bit physical limit.

The result preserves all four slot records and an exact sorted union of enabled
ranges. Overlap and adjacency may merge; gaps do not. No 16KiB rounding occurs.
A future owner policy must reject any full CPU page intersecting these exact
ranges, and must retain provenance for any separate outward rounding it uses.

Success is labeled `MCP_FOUR_PLANES_FOUR_TZ_ONLY`. It is not a global exclusion
census, DMA eligibility, mapping/readiness, signature or actual acquisition
proof. Even four disabled slots do not mean there are no other exclusions.
The historic 80-byte compact list is rejected; it cannot be upgraded into this
new snapshot, and no old receipt or hardware grade is reinterpreted.

Accessible immutable inputs and caller serialization are required. Outputs
cannot overlap inputs and remain byte-for-byte unchanged on every refusal.
There is no allocation, native API, physical pointer conversion, read/write,
retry, firmware action, service, policy `complete` flag or integration backend.
Only ordinary caller output memory is written on success.

Run `python3 -B integrations/puredarwin/m1-apcie-bridge/mcc_snapshot/test_mcc.py`.
Host ASan/UBSan modes include a KERNEL-defined host compile, not kernel execution.
The independent cell-set oracle checks 1,024 bounded unions; negative samples,
alias/atomicity cases and compiled assertion-killed mutants cover the critical
ordering, units, interval and omission errors. NDEBUG test builds are refused.
Actual target objects and independent frozen-source review remain separate.
