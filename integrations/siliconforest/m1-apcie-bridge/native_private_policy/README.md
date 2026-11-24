# Native private policy join — configuration only

This original source composition calls the actual frozen `npr_read_native`,
converts its successful same-call facts into a private backing policy, then calls
`boc_native_derive_and_configure` exactly once. It links the new `native_owned`
0ee owner and c585 registry through owned_control9844, never provider79. The
public API accepts expected identity labels and three domain labels, not a
detached facts object, policy, receipt, raw setup, callback or `complete` claim.

Profile1 is an explicit trust choice: kernel VM owns live PRIVATE IOBuffer
backing inside the inherited BootArgs envelope, subject to known source guards.
It is not a complete physical exclusion census or DMA/hardware authority. The
frozen reader checks the actual inherited envelope, chosen DRAM and retained boot
prefix using only the common40 BootArgs bytes. Expected/source hashes are
trusted declared labels, not authentication. No host facts producer, BootArgs
writer, service, kext entrypoint or target invocation is added here.

`npp_native_configure_once` owns one resident private C atomic phase latch. Pure
argument, identity and overlap failures leave EMPTY. An admitted call transitions
EMPTY→READING→BUILDING→CONFIGURING→CONFIGURED or terminal FAILED. Reentry reports
BUSY; a completed or failed attempt reports USED. No reset, retry or initialization
API exists. CONFIGURING is stored before the external configure call. An error
after pin/configuration may leave retained controls or an already configured
owner. Unchanged public output is not rollback. No cleanup or release is attempted.

Original16KiB-aligned RAM intervals retain their boundaries, including adjacency.
Every full relevant exclusion interval is rounded outward to16KiB with checked
endpoints, sorted, then merged only at actual overlap/adjacency; no holes are
bridged or records clipped to RAM. The resulting reservation capacity is8. Remap
source-only records are never promoted to physical exclusions. The existing
helper adds the actual typed owner and mutex control pages, preserving the
original RAM intervals and checking all whole pages against reservations. A
coarse retained boot prefix can legitimately reserve heap control pages and make
configuration refuse. Capacity/geometry/reservation refusals are final; never
punch holes, shrink the prefix, select a different anchor or retry to succeed.

All large reader/facts/policy/helper/receipt staging lives in a caller-owned
off-stack workspace. Inputs, outputs, state, callback table and declared internal
context extents must be disjoint, accessible and stable. Expected and copied ops
are checked after read and configure; configure receives the fixed staged policy.
The native API supplies only fixed callbacks; the internal C-only seam is for
trusted host tests. It is not a sandbox against arbitrary kernel writes. Public
output is committed only after known configure success and narrow consistency
checks; scratch contents, receipt fields and phase numbers are not authority.

Trusted boot serialization/quiescence, residency/wiring/protection of code,
expected data, workspace, output, latch and contexts, and one owner per physical
domain remain platform obligations. This join does not derive their full physical
protection or global provenance. VM ownership of live private objects cannot be
promoted to a future external DART/effect permission. The owner effect backend
remains absent: an admissible run is UNAVAILABLE before query/begin. This slice
does not call acquire, zero, copy, retire or run and adds no MMIO/DART/IRQ/config
register/firmware implementation.

Run `python3 -B validate.py --output-dir NEWPATH` for host ASan/UBSan, fresh
isolated seam states, actual frozen reader/helper/owner/core under explicitly
synthetic TEST-SDK APIs, compile refusals and compiled assertion mutants. Fresh
TEST states never reset or replace the native global. Three separate combined
processes test configuration, reserved-control refusal and reader cleanup failure;
all make zero backing descriptor allocations. These are not native executions.
`build_objects.py --output-dir NEWPATH` uses only cached genuine headers/toolchain,
compiles O0/O2 and partial-links the10 real TUs, retaining dependencies, frames,
imports and actual atomic disassembly. It builds objects only, never a kext/KC or
target executable. Hardware STOP, GPU HOLD and stopped serial bridge remain.
