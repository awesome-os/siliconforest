# Private UAT allocation and validation

This original leaf owns at most sixteen private 16 KiB table allocations and
validates detached CPU snapshots with the unchanged adjacent UAT codec. It has
host fault tests and ARM64 SDK object compilation. It has **not executed in
XNU or on a GPU**. The explicit GPU observer boot hold remains in force.

There is no kext, start routine, service, node, transport, firmware interaction,
publication API, MMIO, register access, table builder, or write to allocated table
bytes. The IOBMD factory supplies zeroed memory through the inspected XNU path.
Tests construct synthetic host graphs; those are not GPU tables. With the real
provider, a fresh single level-1 allocation can validate as an empty tree;
building nonempty table topology is deliberately outside this slice.

`private.h` declares `duat_init`, `duat_allocate`, `duat_validate`,
`duat_release`, `duat_quarantine`, and `duat_shutdown`. `duat_iokit_ops()` supplies
the real IOBMD provider. No API returns a table VA or PA. Public struct definitions
allow caller-owned storage; their fields are trusted internal state, not a
security boundary against a kernel caller that can write arbitrary memory.

The caller supplies completely zeroed, stable-address `duat_context` storage and
a separate `duat_workspace` outside the kernel stack. All operations require
external serialization in a context permitting blocking; the busy flag catches
callback reentry and is not a concurrent lock. Providers and callers must not
modify pages, context, tokens or workspace during validation. No borrowed pointer
or token may outlive its context; never copy, reset or reinitialize that context,
including at a recycled address while stale tokens exist. No asynchronous path
exists here. Source interfaces do not enforce a hard allocation deadline.

The intended call sequence, shown only as API documentation, is:

```c
struct duat_ops ops = duat_iokit_ops();
/* ctx and workspace: separate suitably aligned caller-owned kernel storage;
 * ctx was zero-initialized, both have stable lifetime; common serialization. */
duat_init(ctx, &ops, NULL);
struct duat_token roots[2] = {{0}, {0}};
/* Check each result before proceeding. */
duat_allocate(ctx, 1, &roots[0]);
duat_validate(ctx, roots, workspace, &summary);
/* Always attempt shutdown after any failure. Retain context/owner if it fails. */
duat_shutdown(ctx);
```

Allocation uses `kernel_task`, `kIODirectionInOut`,
`kIOMemoryPhysicallyContiguous`, `kIOMemoryMapperNone`, capacity 16384 and mask
`0x0000000fffffc000`. It refuses a kernel page size other than 16384. After one
explicit successful prepare, inspection requires exact descriptor length and
capacity, aligned nonnull kernel bytes, a full physical segment requested with
`MapperNone`, nonzero aligned PA, and `PA <= 2^36 - 16384`. Duplicate live PA or
overlapping CPU storage refuses admission. The 36-bit ceiling is conservative
policy; it is not the T8103 hardware width (the acquired reference says 40 output
bits), and the codec's 48-bit field does not prove RAM availability.

Each token binds context address, slot and a nonwrapping generation. Successful
private release permits slot reuse with a new generation. Failed allocation or
validation leaves output tokens/summaries unchanged. Validation builds all ranges
from live owned records, copies all 16 KiB pages to the bounded workspace, checks
the complete graph, then rechecks views, bytes and generations. L1 exposes its
eight meaningful entries to the codec; its allocation range still covers 16 KiB.
There are no data ranges, so every data leaf refuses. Both root handles must be
owned L1 pages or all-zero absent tokens. ASID zero here is detached format data,
not an allocated hardware context. Success is a point-in-time private snapshot
result; no persistent validation ticket or usable device mapping is created.

Only the adapter's explicit successful prepare receives a matching complete;
IOBMD's internal AutoPrepare count belongs to IOBMD. A failed explicit prepare
releases the factory reference without a complete. Any nonzero complete result
permanently retains that allocation and sets sticky failure. `duat_quarantine`
also retains an allocation without completion when exposure is uncertain; it
does not publish anything. No subsequent allocation or validation is allowed in
a failed context. Shutdown still releases other safely private allocations once,
never retries completion of retained pages, and continues returning quarantine.
A repeated provider handle is a contract violation that retains both records.
Retained storage and its owner/code must remain alive; there is no unload or
hardware-quiescence guarantee. Successful shutdown only proves the core has
released its private owner references.

Run `python3 -B integrations/puredarwin/m1-gpu-uat-private/test_private.py` from
any directory; it prints a JSON result and uses ephemeral compiler outputs. It
checks the frozen codec hashes first. `validation.json` records six host groups,
910 checks each under ASan/UBSan and NDEBUG, twenty compiled-and-detected mutants,
zero Clang analyzer diagnostics, three ARM64 SDK objects, exact transitive header
hashes, and their external import/KPI audit. No target executable is run.

Virtual IOBMD methods dispatch through the vtable and therefore do not appear as
direct undefined imports. Their pinned declarations/exports are recorded in the
design report. Neither object compilation nor an export-list match establishes
KC bindings or a runtime ABI result. GPU publication remains blocked on context
ownership, actual GPU-visible address rules, permission/cache attributes,
ordering, invalidation acknowledgements, firmware coordination and real device
quiescence. Nothing in this leaf relaxes those blockers or the boot hold.
