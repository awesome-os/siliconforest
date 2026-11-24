# Private data ownership and detached leaf plans

Original, bounded source-only composition for the frozen UAT table builder.
It owns at most 16 independent 16KiB DATA allocations and validates at most 16
one-page mappings in a detached snapshot of at most 16 owned TABLE pages.
There is no live graph mutation, publication ticket, MMIO, cache/TLBI,
firmware object, service, kext entry point or hardware execution path.

Allocate a stable zeroed `udata_context`, and call `udata_iokit_init` with the
live builder and its exact `uatb_ticket`. The separate DATA provider uses
IOBufferMemoryDescriptor, explicit MapperNone, contiguous 16KiB allocation,
conservative PA below 2^36, explicit successful prepare/complete pairing and
whole-page zero/readback. A data token is a distinct C type and owner domain;
the frozen table allocator is never used to allocate data.

The caller must serialize all table owner, builder, data owner and provider
operations together. All owner objects and code remain at stable addresses;
the borrowed graph storage must outlive the data owner and every plan call.
Do not copy/reset/reinitialize owners, tamper with their records, hand their
storage to firmware or expose their references to any asynchronous consumer.
The internal structures and callbacks are trusted kernel code, not a security
boundary against arbitrary kernel writes. `busy` detects reentry, not threads.

`udata_allocate` exposes a token only after current table/data identities,
physical spans and CPU spans are disjoint, the complete data page is zero, and
the exact prepared view is rechecked. No zero is issued for an aliased or stale
view. `udata_plan` accepts canonical aligned 40-bit VAs and live data tokens,
with permissions exactly `UD_READ` or `UD_READ | UD_WRITE`. EXEC, unknown bits,
duplicate VA/data aliases, absent L3 paths and reserved policy inputs fail.
No caller-supplied PA, descriptor word or raw permission flags are accepted.

Allocate `udata_workspace` on caller-owned heap/static storage, **never the
kernel stack**, and pass its exact capacity. The size gate and owner/scratch
alias gates precede every scratch write. Pass up to 16 `udata_mapping` records
and a separate `udata_summary`. The unchanged builder validates and copies its
actual table graph, this leaf inserts descriptors in the copied pages only,
and the unchanged codec validates the complete snapshot with distinct data
ranges. Original table pages are byte-for-byte unchanged. The summary is
point-in-time statistics; neither it nor retained scratch grants any future
permission to install tables or access hardware. Scratch is unspecified after
a failed call; the summary is unchanged.

Call `udata_release` per token or `udata_shutdown` for private cleanup. Graph
closure/staleness blocks new allocations and plans, but does not block release
of independently owned private data references. A failed allocation, atomic
failed prepare, initial non-writing inspection/admission refusal, or explicitly
private zero failure can roll back a known owned reference. Complete is called
only for a successful explicit prepare. An uncertain zero, changed/failed
previously admitted view, duplicate handle or uncertain complete retains the
affected allocation, makes the owner sticky-failed and forbids retry/reuse.
Other known private allocations can still be released. There is no API to
reclaim retained storage and no unload, GPU-idle or quiescence claim.

The provider contract requires an independent owned reference per allocation,
atomic unsuccessful prepare, non-writing inspect and an accurate private versus
uncertain zero result. Initial disjointness rejection completes/releases only
that distinct owned reference; equal opaque handles are quarantined before
prepare. This cannot recover from a provider fault that has already written
foreign memory, kernel faults during zeroing, concurrent mutation, or dishonest
ownership reports. The actual IOBMD provider rechecks its complete view before
zeroing; no foreign or inherited page source exists in this leaf.

Run `python3 -B integrations/puredarwin/m1-gpu-uat-data/test_data.py` from the
repository. Host fake providers test bounds, alias-before-zero, stale owners,
permissions, unchanged tables, fault cleanup and retained references. Compiled
mutants must fail an explicit test assertion. SDK compilation produces ARM64
relocatable objects only, checks direct external KPI imports, and rejects stack
frames over 2048 bytes. Large snapshots stay in caller storage. No target runs.

GPU observer HOLD remains absolute. Firmware ABI selection remains only the
existing V13_5/G13 reference-profile inference; this leaf does not resolve real
GPU context ownership, publication, coherency, firmware handoff or execution.
