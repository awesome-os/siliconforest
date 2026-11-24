# Bounded T8103 DART software codec

This directory constructs and validates **ordinary RAM values only**. It is
not an IOMapper, a DART driver, a DMA allocator, an initialized stream or proof
of invalidation. No function maps an aperture, touches MMIO, allocates physical
memory, publishes a table, enables a requester or releases backing memory.
It is not connected to any kernel, host launcher or boot packet.

The original implementation uses the pinned MIT m1n1 format as its primary
reference. Linux provides a narrower T8103 profile and corroborating permission
and PCIe fields. Exact pins, hashes, source locations and licensing boundaries
are in [PROVENANCE.json](PROVENANCE.json). Original-code license selection remains
pending. No upstream source is relicensed.

## Accepted format

| Item | Canonical software profile |
| --- | --- |
| Granule / entry | 16,384 bytes / little-endian 64-bit entry |
| Physical range | `[0, 2^36)`; every address must cover an entire aligned page |
| IOVA range | `[0, 2^32)`; high bits rejected, never truncated or wrapped |
| TTBR | valid bit31 plus PA shifted right12; other profile bits zero |
| Root selection | TTBR0 only; TTBR1..3 must be zero |
| L1 / L2 index | bits31:25 / bits24:14; byte offset bits13:0 |
| Table pointer | PA bits35:14 and valid bit0; all other bits zero |
| Full-page leaf | PA bits35:14, valid bit0, SP-disable bit1, SP-start0, SP-end0xfff |
| Permissions | NO_READ bit8, NO_WRITE bit7; R, W or RW only |
| Invalid entry | exactly zero; nonzero invalid entries are rejected |

These are **canonical encodings for this prototype**, not a universal decoder
of firmware tables. m1n1's Python table encoding sometimes sets bit1 on an L1
entry; its C path uses PA|VALID. This validator accepts the latter only.
It also refuses partial subpages, no-access leaves, 4K pages, larger address
widths, other generations and high IOVA aliases. Rejection of a noncanonical
encoding does not assert that hardware reserves every rejected bit.

The APCIE0 range reported from the existing captured ADT is
`[0x00100000, 0xbff00000)`, size `0xbfe00000`. The last eligible page begins at
`0xbfefc000`. Callers must supply an aperture explicitly; the constants do not
discover a device. Every policy additionally excludes
`[0xffffc000, 0x100000000)`, the entire 16K page containing the reported MSI
doorbell `0xfffff000`. No doorbell mapping or MSI configuration is implemented.

## Use and ownership contract

`dld_ttbr_encode`, `dld_table_encode` and `dld_leaf_encode` produce scalar
values. `dld_word_store` serializes exactly eight bytes into caller-owned normal
memory. Decode functions and `dld_split_iova` have no hidden allocation/state.
Physical address zero is representable; it is not an unmapped sentinel.

For snapshot validation, supply a `dld_policy` containing explicit RAM and
reserved physical intervals, and a `dld_image` with the four TTBR values and
the complete list of retained table buffers, their physical addresses and
levels. Each buffer must be exactly16K. RAM and reserved lists each reject
internal overlap and wrapping. A page must fit entirely in one allowed RAM
interval and intersect no reserved interval. Unaligned byte-sized reservations
are allowed and exclude any intersecting page.

`dld_validate` checks the entire graph, including entries unrelated to a later
lookup. It rejects missing/orphan/shared/cyclic/wrong-level tables, duplicate
table pages, table/data overlap, duplicate physical leaf pages, noncanonical
entries and mappings outside the aperture. There is one translation per IOVA
slot; a table cannot be reused under another slot. No physical aliases are
supported. All tables must be reachable exactly once from TTBR0. A fully empty
image uses all-zero TTBRs and zero tables; it validates as empty, not initialized.

Bounds are explicit software limits: at most129 table pages,256 mapped pages
(4MiB),64 RAM intervals and64 reserved intervals. Validation scans at most129
tables and performs a bounded pairwise physical-leaf check. These limits do not
describe device capacity. The caller supplies `dld_scratch`; no kernel stack
allocation or heap allocator is hidden in the implementation.

`dld_translate` revalidates the complete snapshot on every call, then performs a
software lookup with R/W permission checks. It never walks host physical memory.
`DLD_UNMAPPED` is distinct from an error and leaves the result unchanged.
Stats, scalar outputs and translations remain unchanged on failure. Scratch
may change on failure and conveys no hardware ownership or readiness state.

The caller must hold every table buffer, descriptor, policy interval and image
immutable for each call, serialize access to scratch, and keep scratch/outputs
disjoint from inputs. C pointer validity and buffer lifetime are caller
preconditions. No concurrent mutation, atomic publication, revoke, unmap or
reuse protocol is supplied. Never pass volatile/device memory as a buffer.

`dld_rid` checks a full BDF before forming a16-bit RID. `dld_bindings_check`
requires explicit inclusive bus bounds, SID0..15 and slot0..63. It refuses
duplicate RIDs, slots or SIDs. It does not infer that RID0x100 maps to SID1, that
ADT mapper phandle0x50 is a stream, or that a RID mask owns every function it
covers. Linux's shared Wi-Fi/BT stream is deliberately unsupported by this
exclusive-stream prototype. No RID2SID register value is written or installed.

## Host checks

```sh
python3 integrations/puredarwin/m1-dart/test_codec.py --mutations
```

The runner builds only original host code with warnings-as-errors, ASan and
UBSan. LeakSanitizer is disabled for the known ptracer restriction; ASan and
UBSan remain active. Temporary host artifacts stay under this directory and
are removed. Semantic assertion failures must kill every compiled mutant;
compiler failures and sanitizer/environment failures do not count as kills.
No upstream module with a transport backend is imported.

Coverage includes golden words and endian bytes,4096 deterministic roundtrips,
all reserved-bit positions, overflow/alignment, output preservation, maximum
indices, exact APCIE boundaries, the16K MSI exclusion, missing/orphan/shared
tables, physical aliases, reservations, all-or-nothing lookup validation and
RID/SID/slot limits. These checks establish software behavior only.

## Required before a future kernel adapter

1. Verify the exact T8103 DART instance, actual granule/widths, stream count,
   lock state, active roots/remapping and firmware owner before any access.
   Resolve live ADT mapper/phandle and PCI BDF binding; do not adopt this
   prototype's constants as authority. Preserve firmware-owned tables and
   unrelated streams, including BT and display DARTs.
2. Establish exclusive requester/stream/RID-slot ownership, power/clock/reset
   ordering and fault delivery. Bus-master disable alone is not proof that
   in-flight device transactions have drained. No bypass fallback is allowed.
3. Allocate and wire real16K table/backing pages with physical addresses and
   lifetime guarantees. Supply complete DRAM/carveout/reservation bounds and
   reject table/backing overlap. Kernel CPU virtual addresses are not PAs.
4. Implement concurrency and atomic publication, cache-clean/coherency rules,
   barriers, stream-scoped invalidation and bounded BUSY completion. The
   reference's timeout print is not proof of invalidation; timeout must remain
   failure, with no reuse or free of possibly reachable pages.
5. Add real IOMapper/IOPCIFamily/IODMACommand contracts, mapping rollback,
   permissions, MSI reservations and fault containment. This code has no IOVA
   allocator, map/unmap transaction, hardware access control or DMA completion.
6. Require proven device quiescence and completed invalidation before unmap or
   reclamation; quarantine on uncertainty. Keep VM/mapped backing quarantine
   unchanged. Define reset/sleep/unload ownership separately; none is supplied.
7. Review/build the adapter against the pinned kernel and qualify it in
   separately authorized bounded runtime tests. Host tests, table bytes,
   symbols or a passing validator are never evidence of working DART/DMA,
   GPU or Wi-Fi.
