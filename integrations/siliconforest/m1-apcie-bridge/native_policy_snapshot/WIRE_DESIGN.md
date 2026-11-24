# Bounded platform facts wire, proposed revision 001

This is an original metadata format for a future trusted boot producer. It
does not grant physical-memory or device access. Structural validity is not
source authenticity, exclusion completeness, or permission to configure the
native owner. No producer, registry reader or runtime adapter is supplied here.

All integers are little-endian. The header is exactly 1280 bytes; each exclusion
record is 32 bytes. The exact total is `1280 + 32 * record_count`, at most 9472
bytes. Unknown values, trailing bytes, nonzero reserved bytes, and incomplete
required classes refuse. Decoder workspace belongs in caller-owned heap/static
storage, with one output commit after validation. Ordinary accessible immutable
input and boot-phase serialization remain caller obligations.

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 8 | Magic `BCMFCT01` |
| 8 | 4 | Version, 1 |
| 12 | 4 | Exact total bytes |
| 16 | 4 | Board profile, 1 = T8103 |
| 20 | 4 | CPU page shift, 14 |
| 24 | 4 | Physical address bits, 36 |
| 28 | 4 | Wire profile, 1 = native1152 R2/V2 |
| 32 | 4 | Original RAM interval count, 1..8 |
| 36 | 4 | Exclusion record count, 0..256 |
| 40 | 4 | Required source descriptor count, 6 |
| 44 | 4 | Raw MCC bytes, 384 |
| 48 | 4 | MCC coverage, 1 = four planes/four TZ slots only |
| 52 | 12 | Reserved, zero |
| 64 | 24 | Same-boot nonce, nonzero |
| 88 | 32 | Selected KC SHA-256 |
| 120 | 32 | Ramdisk SHA-256 |
| 152 | 32 | HV source SHA-256 |
| 184 | 32 | Base ADT SHA-256, excluding this future property |
| 216 | 32 | Original BootArgs SHA-256 |
| 248 | 32 | Final captured native BootArgs SHA-256 |
| 280 | 40 | Reserved, zero |
| 320 | 16 | Chosen DRAM: base, bytes |
| 336 | 16 | Original guest-managed envelope: base, bytes |
| 352 | 16 | Retained boot reservation: base, bytes |
| 368 | 128 | Eight original RAM `(base,bytes)` slots |
| 496 | 288 | Six 48-byte source descriptors, in tag order |
| 784 | 384 | Raw MCC snapshot, exact frozen mcc_snapshot order |
| 1168 | 112 | Reserved, zero |
| 1280 | 32*N | Exact source exclusion records |

The six identity digests and nonce must match explicit trusted expected values;
none may be all zero. This comparison does not authenticate an untrusted
caller. The wire declares no `complete`, `approved`, or runtime-success flag.
The final BootArgs hash requires an actual future captured native wire, not a
hash inferred from historical legacy bytes. Historical saved records therefore
cannot qualify as a fresh complete input to this format.

Each source descriptor is `(tag:u16, status:u16, count:u32, source_sha256:32,
reserved:8)`. Tags are 1 MCC_TZ, 2 PMAP_IO, 3 FRAMEBUFFER, 4 DEVICE_PHYS,
5 REMAP_SOURCE_ONLY, 6 RETAINED_BOOT. Status is 1 observed-empty or 2
observed-nonempty, exactly consistent with count. Missing, unsupported, unknown
or unobserved required sources refuse. Each source digest is a nonzero declared
provenance label: it is not called recomputed or verified against raw source
bytes that this format does not carry. The source producer's coverage still
requires separate admission; a syntactically empty class cannot prove absence.

Each record is `(tag:u32, ordinal:u32, flags:u32, auxiliary:u32, base:u64,
bytes:u64)`. Records are ordered by tag and then source ordinal, with no
duplicates. Classes other than MCC use dense ordinals starting at zero. MCC
ordinals are the enabled slot indices, preserving gaps. Every range is nonempty,
overflow-free and below 2^36. PMAP_IO retains its raw WIMG and signature in
flags/auxiliary; other classes require zero flags/auxiliary in this revision.
No source record is dropped to meet the separate eight-range union capacity.

DRAM starts at the frozen T8103 base. Its boundaries are 4KiB-aligned. The
managed envelope and original RAM boundaries are 16KiB-aligned and inside DRAM.
Original RAM intervals are ordered, disjoint, inside the envelope, and remain
separate when adjacent. Unused RAM slots are zero. The boot reservation is
16KiB-aligned and wholly inside one original RAM interval. Class 6 has exactly
one record equal to that reservation; class 3 has exactly one framebuffer
allocation record. Those are declared observed extents, not recomputed video
geometry or ownership attestations.

The C decoder must invoke frozen mcp_decode on all 384 MCC bytes. Class 1 must
exactly match its enabled slot records, including original 4KiB boundaries;
four-TZ coverage must never be relabeled global coverage. Class 5 ranges remain
source-only numerical observations: they are preserved but never promoted into
physical exclusions without an independently admitted interpretation.

For classes 1, 2, 3, 4 and 6, preserve every source interval. A separate derived
union may include full intervals that intersect any original RAM interval,
merging only overlap or adjacency. Never clip an interval inward or fill gaps.
Refuse if the exact union needs more than eight ranges. Keep the raw records
and original RAM intervals alongside the union. The resulting object is facts
only and has no conversion to bpl_policy or call to native configure.

Expected tests include encoder/decoder roundtrips, independent interval oracle,
all reserved bytes, every identity and count, missing/empty source distinctions,
old80-byte MCC refusal, malformed384-byte MCC, original RAM adjacency, union8/9,
full-boundary exclusion preservation, alias/overflow and unchanged output on
every failure. Compiled mutants must fail assertions, not merely fail to build.
