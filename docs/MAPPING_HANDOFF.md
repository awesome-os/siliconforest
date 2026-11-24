# Pager lifetime handoff and the XNU adapter boundary

## Implemented here

`core/mapping_ticket.{h,c}` manages one CPU-mapping hold from preparation through
pager notification and serialized drain. It neither constructs a pager nor wires,
maps, enumerates, translates or frees physical pages. It uses the existing
backing-store coordinator, not a second lifetime policy.

The release function has the `void (*)(void *)` shape accepted by PureDarwin's
`pd_dri_ops.mmap_pages` interface. Its body performs one lock-free atomic store.
It does not dereference the store, allocate, take locks, schedule a worker, or
invoke a destructor. The serialized control/worker path drains the hold; actual
backing destruction remains a separate `dl_store_collect` operation.

A ticket is a stable-address, **single-use**, caller-owned object. Zero-initialize
it before prepare. Do not copy, move, reset or reuse a published ticket. Keep it
alive until the external callback producer has been detached and joined, and the
ticket has drained. A `DRAINED` state alone is not proof that all invocations have
returned. No API here fabricates that proof. The implementation contains no
recycling pool, kernel reference management, module unload protection or workloop.

Duplicate callbacks are idempotent only while the ticket remains alive. A late
call through an already freed cookie remains a use-after-free; this library does
not claim to make such calls safe. The native concurrency test explicitly joins
all callback threads before ticket storage goes out of scope.

## Source observations at the candidate PureDarwin revision

Revision: `e7aa00a60f0b56ef35aa24252e81467787bf71ea`.
These are inspected source ranges, not full-file byte verification or execution.
See `reports/sprint-003/source-evidence.json` for paths, blob IDs and scopes.

1. `PDDRMDri.cpp` forwards its mapping request to the owning provider's
   `mmap_pages`, including release function and argument outputs.
2. `bsd/kern/kern_mman.c:300-390` calls the provider to enumerate pages. On
   provider failure, the caller frees its page-number array and returns: it does
   **not** invoke a returned cleanup callback. Provider-local failures therefore
   must roll back their own hold before returning an error.
3. On provider success, that code attaches the callback to a device pager before
   populating pages. Population and memory-entry failures deallocate the pager.
   A published hold must therefore survive both successful mapping and failed
   setup until that pager reports release.
4. `osfmk/vm/device_vm.c:390-455` invokes `pd_release` on the named-reference
   teardown path after asserting `is_mapped == FALSE`. It clears the callback
   after invocation and then destroys the memory object. This is **not simply a
   file close callback**, nor does it prove the whole kernel call has returned.
5. `IOBufferMemoryDescriptor.h:180-246` describes `inTaskWithOptions` for shared
   I/O buffers, notes that allocation may block, and describes default kernel
   memory as wired unless pageable memory is requested. This provides an
   allocation candidate, not a verified provider implementation.

Sources:
- https://github.com/PureDarwin/PureDarwin/blob/e7aa00a60f0b56ef35aa24252e81467787bf71ea/src/Kernel/Extensions/PDDRMDri/PDDRMDri.cpp
- https://github.com/PureDarwin/PureDarwin/blob/e7aa00a60f0b56ef35aa24252e81467787bf71ea/src/Kernel/xnu/bsd/kern/kern_mman.c
- https://github.com/PureDarwin/PureDarwin/blob/e7aa00a60f0b56ef35aa24252e81467787bf71ea/src/Kernel/xnu/osfmk/vm/device_vm.c
- https://github.com/PureDarwin/PureDarwin/blob/e7aa00a60f0b56ef35aa24252e81467787bf71ea/src/Kernel/xnu/iokit/IOKit/IOBufferMemoryDescriptor.h
- https://developer.apple.com/documentation/kernel/iomemorydescriptor/1812845-prepare
- https://developer.apple.com/library/archive/documentation/DeviceDrivers/Conceptual/IOKitFundamentals/DataMgmt/DataMgmt.html

The Apple documentation distinguishes descriptor memory preparation from DMA
command preparation. Neither a retained descriptor nor an integer physical page
number is by itself a correct DART/IOVA mapping. Do not feed host physical
addresses to a device that requires translated addresses.

## Proposed adapter sequence, not a working kernel implementation

1. Resolve a per-open handle and check authorization. Our `allowed_access` field
   is supplied by a trusted adapter; it is not an authentication mechanism.
2. Under the store's common serialization, prepare a ticket and acquire its
   CPU-map hold. This restricts offsets/sizes to the configured page granule and
   rejects ranges outside the rounded, zero-initialized backing allocation.
3. Obtain verified host physical segments from the retained backing provider,
   produce the kernel's page list, and choose reviewed ARM64 cache attributes.
   Do not infer a cache encoding from x86 constants or assume contiguity.
4. On any driver-local failure, abort the unpublished ticket. Publication is the
   final driver-side ownership transition before returning success. On success,
   publish the function/argument pair to the pager. A normal error return after
   publication would violate the ownership contract.
5. Pager release only records a notification. Arrange a kernel worker or polling
   mechanism independently. The current code intentionally has no scheduler.
6. The serialized worker drains the mapping hold. Collection may free the backing
   only when all host/device/firmware references and quarantine rules permit it.
7. Retire ticket storage and the module only after the callback producer is
   provably detached/joined. This barrier must come from the real adapter.

## Integration gates still open

- Reproduce an unmodified PureDarwin ARM64 VM before applying this design.
- Compile against the actual target XNU headers and link a RAM-only test driver.
- Review the entire mapping call site, including authorization/max-protection,
  map inheritance, `mprotect`, fork and mixed user/kernel page-size behavior.
- Verify the pager's unmap/TLB/cache boundary and whether backing collection can
  race the memory-object destruction after the release callback. Deferred work
  alone does **not** prove that a kernel teardown has finished.
- Design ticket allocation, worker scheduling, module retention and shutdown
  joining. All non-callback operations still require external serialization.
- Implement/inspect the real provider's allocation, preparation, zeroing, segment
  enumeration and destruction. `complete()`/release must obey the provider's
  atomic-failure contract; do not assume retries are safe after partial teardown.
- Run real mmap/unmap, descriptor-close, fork/exit, partial-unmap, failed-entry,
  pressure and unload tests in the guest. Host tests do not test these VM paths.

## Why the page-number helper exists

The inspected PureDarwin device interface uses a 32-bit page-number array.
`dl_map_page_number` checks alignment and narrowing at a caller-selected 4, 16 or
64 KiB granule. It does not detect the kernel page size or identify an address
space. Its input must already be a verified host physical address. The broader
Asahi project still requires target-specific 16 KiB validation. None of the
helper's accepted granules establishes device support.

## Executed tests

Fourteen new native groups use real host backing allocations and **simulated**
pager notifications. They cover rollback on both sides of publication, close
before unmap, independent maps, duplicate notifications with live tickets, reused
buffer slots, simulated timeout/reset, capacity, range/permission checks, integer
narrowing, and eight concurrent callback threads. Callback threads never access
the non-thread-safe backing store.

The same groups ran with AddressSanitizer/UndefinedBehaviorSanitizer and separately
with GCC ThreadSanitizer. No diagnostics were reported in those runs. Passing a
stress run or a sanitizer is evidence, not a proof of every interleaving or kernel
context. Six deliberate source regressions compiled and were caught at runtime.
