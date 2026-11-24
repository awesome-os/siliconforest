# Kernel CPU control protection derivation

This original source derives protection metadata for the frozen private backing
provider's typed anchor and underlying mutex. It contains no MMIO, physical
dereference, PTE/DART write, firmware, IRQ, activation or data-buffer allocation
path. It does not clear hardware STOP or establish DMA ownership. No native
translation or configuration was executed by the host tests.

Design: `bcm4378-control-protection-derivation-design-001.json`, SHA256
`51502d598c34ca88e0e6129fba88355cfc58e8de99fb7a9e6fae7c22b6321ca4`.
Direction review: `bcm4378-control-protection-design-independent-review-001.json`,
SHA256 `6381b6b6b946db1337a24c55b50e9632c5b2bd994b1a8750721f488e897b97df`.
The frozen17-file native provider79b153 and five-file lifecycle c585fd remain
unchanged external dependencies. No external driver implementation was copied.

## Real entry point and trusted sequencing

`bcp_native_derive_and_configure(base_policy, workspace, out_receipt)` accepts no
caller setup address or detached receipt. It first validates the bounded base
policy and input/workspace/output disjointness without native calls or writes.
It then internally calls the exact frozen `bpn_native_pin_empty` once, derives
from that actual returned identity, and calls `bpn_native_configure` once with
the same setup and exact generated policy/protection. The native pin callback
latches its attempt before calling the provider. No retry, fallback or repin
follows success, failure or nonreturn. Pure invalid input rejected before pin
does not consume that attempt. This is trusted single-threaded boot setup before
any provider clients, waiters, free, remapping or unload.

Known configure success is required before the public receipt gains configured=1
and is committed. Failure leaves public output byte-for-byte unchanged; scratch
may contain incomplete or derived state. Configuration is not rolled back: an
unexpected post-configure consistency failure retains the pinned controls and
stops the call path. No data descriptors are allocated or prepared by this
wrapper. A failure must not be treated as permission to use the raw provider.

`bcp_derive` and `bcp_boot_configure` are lower-level trusted/internal/test APIs.
Their callbacks and any supplied setup must be immutable and held by trusted
platform code; they are not interfaces for user-provided kernel addresses.
The native entry point's internally owned pin closes the earlier problem that
a final configure identity check could not authenticate preceding pointer
queries. Privileged raw calls to frozen `bpn_native_configure` remain technically
possible and outside this checked call-path contract. This is not a security
boundary against arbitrary privileged writes or forged workspaces.

## Exact native types and Unsupported API dependency

`control_native.cpp` uses actual `IOLockGetMachLock`, `kernel_pmap` and
`pmap_find_phys`. `native_pmap_bridge.h` repeats only the exact source-bound
extern-C declarations missing from exported headers, using real private KDK
types. Production builds must force-include the exact rxi private
`mach/vm_types.h`, then private `arm/locks.h`, before public headers under the
admitted KERNEL/KERNEL_PRIVATE C++17 profile. Public/private ARM lock headers
share an include guard, so include ordering matters. No full private kernel
layout selectors, invented opaque type or pasted structure is used.

Static assertions require ARM64 pointer widths, `sizeof(lck_mtx_t)==16`,
alignment8, ppnum32, addr64 and exact function signatures. The native getter
must return the same nonnull pointer as the held opaque mutex identity. This
uses the typed16-byte underlying mutex, never sizeof(IOLock) or a guessed
allocator bucket extent. Those facts are source/type-profile bounds, not a
runtime discovery of slab, turnstile or allocator metadata.

A future linked kernel artifact requires explicit `com.apple.kpi.unsupported`
dependency for `_kernel_pmap` and `_pmap_find_phys`, and its actually used IOKit,
Mach/Libkern dependencies. Root owns the genuine frozen-source framework build,
mixed link, dependency/import checks and packaging. Merely finding symbol names
does not admit execution. There is no module initializer in this helper.

## Bounded derivation and policies

All receipt, candidate, policy snapshots and page vectors live in caller-owned
heap/static `bcp_workspace`, not a large kernel-stack aggregate. Only small
pre-getter snapshots are automatic, because scratch cannot be written before
the underlying control spans and alias checks are known. Output must not
overlap workspace, inputs or either exact control object. Accessible pointers,
stable lifetimes and serialized output access remain caller obligations.

The anchor byte span may be unaligned. Its identity must equal its base and its
checked extent must be nonzero. The mutex is aligned8 with exact extent16 and
may cross a16KiB page boundary. Byte-overlap of the two distinct objects is
refused; sharing a covering CPU page is allowed. Every intersecting CPU page is
enumerated without unchecked round-up, sorted and deduplicated. More than eight
unique pages fails before page lookup; no truncation or repeated-until-stable
loop is used.

Each page base is queried twice in the same order, at most sixteen page queries.
Source profile, underlying mutex, kernel pmap identity and immutable setup/policy
are captured and checked at boundaries. Both runtime page-size forms and shift14
are required. A nonzero ppnum must fit22 bits before conversion by
`(uint64_t)ppnum << 14`; the full page must fit below2^36. Physical zero/unmapped
ambiguity is conservatively refused. The code does not use a DART TTBR shift or
derive PA from VA. Changed/failed lookups or source observations reject without
retry and without output change.

Every full PA page must fit within one ORIGINAL declared RAM interval and avoid
all base reservations. RAM ranges are sorted but never coalesced before or after
that membership test; adjacent subranges cannot jointly authorize a page that
fits neither alone. Base intervals must be valid, nonoverlapping and have zero
unused tails. Existing reserved pages are not waived as already-owned controls.

Distinct control VA pages may map to one PA page. The ordered receipt preserves
every VA/ppnum pair while protection uses the exact PA union once. CPU/PA unions
and final reservations merge only overlap or immediate adjacency, never gaps.
All original domains, RAM intervals and reservations remain in the final policy.
If any protection or final reservation list exceeds eight entries, derivation
fails. No range is dropped or widened across a hole to fit the bound.

## Receipt semantics

The receipt carries full setup/source/type facts, fixed provider/core/type-profile
source hashes, ordered CPU pages/ppnums/PA pages, canonical base/final policies
and exact protection arrays. Nonzero protection provenance is the held setup
identity label, not a cryptographic authenticator. A separate candidate is
checked field by field against captured source state and canonical products,
including recomputed unions and original policy interval sets. Structure padding
is never used for policy identity. Public output is copied only after all checks
and, on the native path, known configure success. This is a caller-serialized
transaction, not an atomic multiword publication for concurrent readers.

`bcp_check_workspace` checks internal consistency only; it does not authenticate
a detached receipt. The native wrapper never accepts one. Two equal mapping
passes show consistency between samples, not immunity to later remapping. Boot
lifetime wired ownership and the no-free/no-remap sequencing contract remain
essential. Coverage is the exact typed anchor/mutex plus their intersecting full
pages, not every transitive kernel allocation or allocator data structure.

## Host evidence and limitations

Run `python3 -B integrations/puredarwin/m1-apcie-bridge/control_protection/test_control.py`.
The actual algorithm is tested under ASan/UBSan with instrumented read-only
fixtures, unchanged frozen core policy admission, bounds/alias/failure cases,
receipt mixing and compiled semantic mutations. The actual native adapter is
also executed against explicitly original TEST SDK/API stand-ins. Those tests
do not call native kernel APIs, derive real physical addresses or substitute fake
headers in the parent's real build. NDEBUG test builds are refused.

The final source receipt records the two initial fixture assertions and their
corrections, final test counts, source/dependency hashes and host frame evidence.
Independent source review and the genuine ARM64 build are separate receipts.
No helper runtime, private allocator experiment, hardware boot or new packet is
admitted by these artifacts. Shared PHY/PMGR writes remain disabled and GPU HOLD
remains absolute.
