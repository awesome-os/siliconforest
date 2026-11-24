# Private UAT table graph builder

This original leaf constructs bounded table topology in exclusively owned,
private CPU memory. It composes the frozen UAT allocator and codec without
modifying them. Host tests execute writes to fake pages; the real IOBMD bridge
has only been compiled as an ARM64 object. **No target code has executed.**
The explicit GPU observer boot hold remains unchanged.

The result is intentionally incomplete for a GPU: every L3 entry is zero and
unmapped. There are no data leaves, permission flags, inherited firmware tables,
TTBR publication, cache operations, TLBI, firmware messages, MMIO, service, kext,
device node or boot package. The captured V13_5/G13 firmware profile supplies no
additional runtime authority; this builder only needs the frozen G13 table format.

`builder.h` declares `uatb_init`, `uatb_build`, `uatb_validate`,
`uatb_quarantine`, and `uatb_shutdown`. The internal adapter header also declares
`uatb_iokit_init`, which binds the new mutable bridge specifically to unchanged
`duat_iokit_ops()`. The generic initialization API exists for trusted providers
and host tests; never pair an arbitrary provider handle with the IOBMD writer.

The input is one complete logical topology of1..16 nodes:

```c
const struct uatb_node plan[] = {
    {1, UATB_NO_PARENT, 0, 0}, /* detached root0 -> L1 */
    {2, 0, 7, 0},             /* L1 entry7 -> L2 */
    {3, 1, 2047, 0},          /* L2 entry2047 -> empty L3 */
};
```

For L1, parent must be `UATB_NO_PARENT` and entry selects detached root0 or1.
For L2/L3, parent is an earlier node of exactly the preceding level; entry is
bounded to8/2048 respectively. No two nodes can occupy the same parent/entry.
Forward references, cycles, shared parent entries, duplicate roots, invalid
levels and any nonzero permissions refuse before allocation. These index bounds
define unique G13 virtual subranges without accepting caller addresses. Both
root halves are supported as detached private metadata; using root1 here does
not adopt the live firmware TTBR1. ASID0 is the frozen snapshot's detached
encoding, not an allocated hardware context.

The builder exclusively owns one embedded `duat_context`; callers never supply
raw PA, VA, descriptor bytes, preexisting allocation tokens or foreign tables.
It allocates every page itself with the frozen provider and keeps each exact
context/slot/generation token. Allocation retains the16KiB page/alignment/full
segment checks, conservative PA<2^36 admission policy, and duplicate physical or
CPU-span refusal. A future data mapping layer needs a separate typed data owner;
it must not reinterpret these table allocations as data.

The caller supplies completely zeroed, stable-address context storage and a
separate frozen `duat_workspace` outside the kernel stack. All inputs/outputs,
workspace and owner storage must be valid and disjoint. The caller serializes
all operations in a context permitting sleep and exclusively controls the owner,
provider, writer, tokens and snapshots. Struct fields are trusted implementation
state, not a security boundary against arbitrary kernel memory writes. Never
copy/reset/reinitialize the context or recycle its address while tickets exist.
The busy flag rejects reentry, not concurrency. There are no callbacks from
hardware or asynchronous consumers.

Build proceeds as one transaction:

1. Validate and copy the complete plan before allocation.
2. Allocate owned pages and build full zero-based page images in detached
   workspace RAM, encoding only plain intermediate descriptors.
3. Write children before parents through the separate mutable bridge. Before
   granting each write it verifies the exact live token, PREPARED state and
   fresh provider view against the frozen allocation record, then checks the
   source/destination span and overlap. The real writer additionally checks the
   IOBMD's writable pointer, length and capacity before its CPU copy.
4. Compare every allocated page byte with the requested image, including unused
   L1 padding, and run unchanged `duat_validate` on an immutable snapshot. A
   merely well-formed graph with different parent indices is not accepted.

Only full success returns a builder-bound generation ticket. No successful graph
can be rebuilt or incrementally edited. `uatb_validate` requires that ticket,
reconstructs the expected image, compares live private bytes and repeats frozen
snapshot validation. Stale/foreign tickets refuse. A successful validation is a
private point-in-time result, never mapping publication or coherency evidence.

`UATB_FAILED_PRIVATE` from a writer means it may have partially changed the page
but guarantees all bytes remain exclusively private. The builder shuts down the
owner and releases every safely private allocation once. Original failure stays
failed even when rollback releases everything. `UATB_UNCERTAIN`, an unknown writer
result or a stale physical/CPU view permanently quarantines all live allocations.
An uncertain completion retains that allocation while other private pages can
be released. Completion is never retried; no retained allocation is reused.
`uatb_quarantine` provides the same conservative response to suspected exposure.
The owner and code must remain alive while anything is retained. Successful
shutdown only means private owner references were released; it proves neither
GPU quiescence nor unload safety. A kernel CPU fault during copying is not a
recoverable transaction failure, and no such fault-recovery claim is made.

Run:

```sh
python3 -B integrations/puredarwin/m1-gpu-uat-builder/test_builder.py
```

`validation.json` records7 groups/34378 checks per sanitizer/NDEBUG build,
16 compiled-and-detected mutants, zero analyzer diagnostics and5 ARM64 objects
with262 dependency hashes. Tests cover both roots, the16-page limit, forbidden
permissions/leaves, cycles/forward links/duplicate parent entries, full-page
zeroing, exact plan matching, PA boundaries/duplicates, stale tokens/views,
write-span boundaries, partial private failure, completion failure and permanent
quarantine. Builds are temporary object files, not a kext or a target executable.
Virtual IOBMD dispatch and KPI export matches remain compile/source evidence.
