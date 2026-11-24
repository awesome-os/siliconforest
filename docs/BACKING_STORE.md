# Portable backing-store coordinator

`core/backing_store.c` connects the existing lifetime policy to an allocation
provider. The native test provider uses real aligned host allocations and frees.
This is useful implementation code, but it is **not an XNU backing-store adapter**.
No kernel allocator, device memory, page wiring, cache mapping, DMA or GPU command
submission is implemented.

## Ownership and execution context

The coordinator reserves a generation-tagged policy slot before allocating.
The handle is published only if allocation succeeds. Failed allocation must
retain no provider resources. Closing an owner, completing work, releasing a
host mapping or acknowledging quiescence never calls the provider's destructor.
`dl_store_collect` alone does that, and the caller must schedule collection in a
context where blocking destruction is legal.

Collection retains backing while an owner, CPU/kernel mapping, shared reference,
GPU binding, in-flight job or firmware reference exists. A device-lost state
blocks collection for the whole domain. Successful externally verified
quiescence retires device references but never host references.

Provider release is all-or-nothing: false means ownership and all backing remain
intact and retryable. The coordinator preserves the slot and byte charge on
failure. A provider that partially frees memory and returns false violates this
API; this coordinator cannot repair that behavior.

## Concurrency and callback obligations

Every operation requires caller serialization. This is not thread-safe, does
not implement a worker queue, and does not establish a suitable kernel lock
hierarchy. Do not hold a fence-producer or reservation lock across collection.
Provider callbacks may not reenter the store; public operations reject that
reentrancy. Direct mutation of exposed struct internals and reinitialization of
a live store are prohibited.

A real mapping-last-release callback must own a durable token containing the
buffer/session/lease identity. Do not pass a pointer to a reused metadata slot
and assume the token's generation solves pointer ABA. The raw `void *` callback
bridge to PureDarwin is **not implemented** here. The tests use value tokens.

Do not call the acknowledgement operations merely because an unmap, firmware
stop or reset was requested. Their names describe established backend facts.
The sequence number is ordering metadata, not proof that DMA has stopped.

## Deliberately limited semantics

This small initial policy rejects new leases after creator/owner closure. A
production shared-buffer implementation can legitimately create new references
from independently surviving handles. That requires a separate capability/owner
model; this policy is not complete GEM/PRIME semantics. No handle passing,
mmap offset namespace, VM-private objects, fencing implementation, purge/madvise,
imported buffers, pinning, cache policy or hot-unplug teardown is supplied.

Allocation sizes are rounded and charged against a configurable byte budget.
The fixed tables allow 16 buffers and 64 leases. These are bounds for a tested
prototype, not intended workstation capacity limits.

## Connection to inspected upstream code

Omacom `cleanup.rs` explicitly moves reservation-taking destruction away from
fence producers and retains the queue until module/producer teardown boundaries
close. We preserve that principle by making collection explicit, not by copying
its Linux workqueue implementation. A production XNU adapter must still provide
and test the corresponding scheduler/worker, unload and producer-join rules.

- Repository: `omacom/linux-aurora`
- Commit: `6a08a08e57558083f552733a5276d71b1a4d8ff6`
- Path: `drivers/gpu/drm/asahi/cleanup.rs`
- Git blob: `e3dd5ad95dc0ce81f89f9b67f05d7653c2dfe7d9`

PureDarwin `pd_dri_cdev.h` supplies a `mmap_pages` release callback contract.
This is a candidate boundary to adapt, not evidence that our coordinator already
satisfies its XNU pager or cache-coherency requirements.

- Repository: `PureDarwin/PureDarwin`
- Commit: `e7aa00a60f0b56ef35aa24252e81467787bf71ea`
- Path: `src/Kernel/fbsdkpi/pd_dri_cdev.h`
- Git blob: `395083ab8450653ede86772e27e1d3eff4e4b759`

Both complete files were retrieved through the GitHub connector and matched
byte-for-byte to their reported blob identity. Source bytes stay in the excluded
cache; provenance and the narrow inventory are in `reports/sprint-002/`.

## Minimal usage pattern (host example)

```c
/* supply allocation/release functions honoring the header's contracts */
dl_store store;
dl_backing_ops ops = { provider_allocate, provider_release };
/* Check every return value in the real integration. */
int status = dl_store_init(&store, unique_session, 16384, byte_budget, ops, context);
/* allocate -> acquire leases -> actual backend operations -> acknowledgements
 * -> close owner -> explicit collection from a suitable worker context */
```

This is not a proposed kernel entry point and does not configure hardware.
