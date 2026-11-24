# Owned control protection — Step 2 source artifact

This original helper version derives a bounded CPU-control protection receipt
for frozen `native_owned` receipt **0ee2101c…**. It adapts our own frozen
control-protection002 algorithm under `boc_` names. The old helper, provider79,
new owner and canonical c585 lifecycle remain unchanged. There is no BootArgs,
DT or policy-reader code here. Base RAM/reservation policy is still supplied
by trusted setup; its runtime provenance remains unresolved. No native runtime
configuration, allocation cycle or hardware action is admitted by this artifact.

`boc_native_derive_and_configure(policy, workspace, receipt_out)` accepts no raw
caller setup/address, callback table or detached receipt. Pure input/alias/policy
preflight occurs before the native wrapper consumes its single pin attempt.
The wrapper invokes the frozen new owner's internal `bno_native_pin_empty`,
uses its actual `sizeof(NativeAnchor)` span, derives protection, and calls that
same owner's `bno_native_configure` once. Failed/uncertain pin, lookup, binding
or configure has no automatic retry/reset. Output is unchanged on failure;
workspace may change. If configure succeeds but a later consistency check
fails, controls remain retained; output failure does not imply rollback.

The low-level `boc_derive`/`boc_boot_configure` interfaces remain trusted internal
and test APIs. Receipt consistency and a source hash are not authentication
against privileged kernel code. The raw owner configure API is not secretly
made inaccessible by this helper. Old79 labels fail this helper's binding
checks, and the high-level native API has no path that accepts an old detached
receipt. The new source manifest binds the provider hash without a cycle:
frozen owner code contains no hash of this new helper.

The exact typed control objects are the full returned anchor and the private
`lck_mtx_t` returned by `IOLockGetMachLock`: pinned size16/alignment8, checked
at compile time. This covers the anchor's inline manager/ops/context/references
as part of its complete typed extent. It does not guess `sizeof(IOLock)` or
claim complete transitive protection of opaque native allocator objects.
Source/context/code residency and allocator integrity remain platform duties.

The algorithm protects every intersecting 16KiB CPU page, sorted/deduplicated,
with at most8 unique pages and two passes/16 physical queries. The source
profile, page geometry, map identity, setup and original policy must remain
identical between observations. PPNs are nonzero and <=0x3fffff, widened before
`<<14`, and their entire PA pages must lie below36bits within one ORIGINAL RAM
interval. Adjacent RAM intervals are never merged to authorize a crossing
page. Existing reservations cannot overlap newly derived controls. PA aliases
are retained in per-VA observations and deduplicated only in protection unions.
Reserved unions preserve every original interval and every derived page,
merge overlap/adjacency only, refuse holes/truncation and remain <=8 ranges.

All transactional receipt storage is caller-owned heap/static workspace, not
a large kernel-stack automatic. Exact input/output/workspace/typed-control
aliases refuse before scratch writes. Stable copied callback tables are used
through both passes and configure. A complete output is committed only after
known configure success and post-checks. Two matching translation samples are
observations, not a guarantee against later remapping or DMA ownership.

The genuine adapter uses source-bound private XNU CPU translation declarations
`kernel_pmap`/`pmap_find_phys`. They require the explicit Unsupported KPI
boundary; they are not an implicit supported-API escape. Real compilation must
preinclude the exact private `mach/vm_types.h`, then private `arm/locks.h`, before
public headers. Static assertions verify signatures and sizes. Genuine builds
must never include either TEST SDK directory. There are no MMIO, page-table,
DART, IRQ, firmware or activation writes in this helper.

`python3 -B test_control.py` runs ASan/UBSan algorithm/boundary/mutation tests,
the actual new bridge against isolated TEST APIs, and one combined binary
linking the actual new helper, frozen owner C/C++, and unchanged canonical core.
The combined SDK lives only here; no frozen header was edited. It supplies
original stand-ins for kernel APIs, deliberately places the actual owner
allocation across a page boundary, and records the requested allocation size.
Tests compare the receipt to that full actual typed span, exercise lookup and
setup failures/no retry, preserve original RAM boundaries, reject old79 labels,
and reject protected-control PA/CPU aliases before private zero. They exercise
real owner private acquire/zero/copy/known-retire control flow under TEST APIs,
plus prepare/complete failure retention. Native run remains `UNAVAILABLE`,
without descriptor queries or effect history. These are host source-integration
facts, not actual IOKit/CPU translation or hardware observations.

Mutable genuine-framework O0/O2 one-object frame checks are separate evidence.
Parent owns the full frozen-source target compile/link/import review and runtime
decisions. No service/image/packet is built; base-policy handoff and native
runtime setup remain unadmitted. Hardware STOP and GPU observer HOLD persist.
