# Native owned coordinator — Step 1

This original source version joins one canonical `private_lifecycle` lease and
its bounded effect history to the same cohort of retained native references.
The genuine adapter has **no effect backend**: an otherwise admissible
`bno_native_run` returns `BNO_UNAVAILABLE` before descriptor queries, ledger
begin, dispatch or output changes. This is not an activated driver, a hardware
ownership proof, or permission to configure the native owner at runtime.

The implementation adapts our own frozen `native_backing`79 private control
flow under new `bno_` names. The unchanged c585 lifecycle is the only registry.
Old provider79 and control-protection002 are neither edited nor linked into
this owner. The two owners must never be independently installed for the same
physical domain. Registry uniqueness and source/code residency remain trusted
platform obligations, not facts a local registry can discover.

`native_owned.h`/`owned_types.h` expose opaque identity tokens, bounded values,
private-prefix operations and snapshots only. `owned.h` is a trusted/internal
provider interface, not a caller lease, registry or backing accessor. Its
`bno_native_pin_empty`/`bno_native_configure` declarations support source and
host-test setup only in Step 1. No service, entrypoint, packet or call site
configures them on a target. Step 2 must supply a separately frozen protection
bridge/profile bound to this new source receipt and the full new anchor;
control002's hardcoded provider79 receipt cannot authorize this owner.

The native adapter retains actual `OSPtr<IOBufferMemoryDescriptor>` references
inside one independently retained, noncopyable boot-lifetime anchor. It uses
the actual IOKit factory in C++17 shared-pointer mode, three 16KiB allocations,
`MapperNone | HostPhysicallyContiguous`, physical mask `0xfffffc000`, actual
page-size checks and one explicit prepare/complete pair distinct from the
constructor's baseline. Policy/protected CPU/PA ranges and all three exact
spans are checked before zero. The anchor owns the fixed callbacks and their
context; a caller cannot replace them or release descriptors by dropping a
token. Future protection must cover the complete anchor/control footprint and
retain the established opaque-kernel-allocation trust limits.

The C coordinator's injected ops are a trusted internal/test boundary, copied
once at initialization. They do not make an arbitrary callback safe or grant
physical authority. Callers serialize setup; a short mutex protects metadata,
and a domain-wide busy state covers every unlocked native/callback operation.
Reentry refuses. Inputs are bounded values copied before callbacks; outputs
must not alias control/protected or validated owned memory. No public function
returns a descriptor, CPU/PA view, canonical lease or effect ticket.

For the host-only dispatcher, `bno_run` checks token/state/request/permissions,
then immutable backend availability and the remaining ledger/sequence budget.
It holds all references and explicit prepare obligations while rechecking all
three exact identities/spans. It journals the SAME canonical lease before the
one synchronous callback. The internal scope carries the pending ticket and
exact cohort for that invocation only. It is not a transferable capability.
Result generation/sequence/slot/operation/ticket and all identities must match.
Any failed, unknown or mismatched result quarantines and halts the domain while
retaining claims and every still-owned reference/preparation obligation.
Changed bound views also halt: stale overlap metadata cannot admit new work.
A callback that never returns leaves busy/pending ownership retained; no timeout
or cancellation API converts it into success or release.

Fixed operation IDs describe conservative possible-effect unions, not hardware
register or firmware formats:

| ID | Kind | Resources | Bounded value request |
| --- | --- | --- | --- |
| 1 CONFIG | CONFIG | endpoint (8) | offset/length zero |
| 2 MAPPING | MAPPING | root/leaf/data (7) | offset/length zero; READ required |
| 3 DESCRIPTOR | DESCRIPTOR | data/endpoint (12) | WRITE, DATA extent 1..64 bytes |
| 4 SUBMIT | DOORBELL | data/endpoint (12) | WRITE, DATA extent 1..64 bytes |
| 5 INTERRUPT | INTERRUPT | interrupt (16) | offset/length zero |

Reserved request fields and unknown IDs refuse. No request contains a pointer
or arbitrary address. Host dispatch only records synthetic events and, for
IDs3/4, writes ordinary TEST-SDK DATA bytes. It supplies no real mapping,
register, interrupt, firmware, notification or activation implementation.
`BNO_TEST_DISPATCH` requires the original TEST SDK and is refused with `KERNEL`
in both the public value header and native translation unit. The production
adapter's immutable availability is zero and its dispatch pointer is null.

A lease can record at most **32 lifetime effects**. One entry covers exactly
one bounded synchronous dispatch and its fixed conservative resource union;
there is no nested callback, asynchronous borrow, public begin/result pair or
unbounded batch. Each call reserves four sequence values (three rechecks plus
dispatch) before callbacks. The 33rd effect refuses without queries or dispatch.
A begun entry is never refunded, overwritten, wrapped, transferred or reset,
even if a callback refuses before applying anything. Success means only a
reported callback result. Any effect forever forbids private zero/copy,
complete, reset and retirement. Logical closure or a dropped token does not
release backing. This finite test workload is not a continuous driver; a
future continuous lifecycle needs a separately reviewed history/retention
design. This API provides no batch reservation; a future multistep workload
must account for its worst-case budget before adding an implementation.

Only never-exposed, known-private cohorts can complete all explicit pairs,
then retire canonical metadata, then reset refs. Failure stops the sequence;
there is no retry, rollback write, exposed free, force cleanup or unload path.
As in79, an uncertain private reset may already have released a reference;
the owner reports uncertainty and halts, not a false claim that all refs remain.

Run `python3 -B test_owned.py` for original ASan/UBSan tests, actual adapter
source against TEST-SDK stand-ins, compiled mutants and refusal builds. Host
fixtures create/dispose ordinary memory between independent test cases; this
is not runtime recovery. Tests cover pending callback lifetime, exact same
lease/ref identity, pre/post-application faults, refcounts, reentry, mutable
caller storage, private/owned output aliases, sequence boundaries and all32
entries. Compiled mutation failures are counted only after successful compile
and assertion failure. No host result proves IOKit execution on XNU. Mutable
real-framework one-object O0/O2 frame prechecks are retained separately;
parent owns final frozen-source target compile/link/import review. No genuine
build uses TEST-SDK headers. Step 1 introduces no MMIO/PTE/DART/IRQ/firmware
access; hardware STOP and GPU observer HOLD remain unchanged.
