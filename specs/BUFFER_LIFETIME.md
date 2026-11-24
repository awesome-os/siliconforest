# Buffer lifetime: executable reference policy

Implementation: `contracts/buffer_lifetime.{h,c}`. Tests:
`tests/c/test_buffer_lifetime.c`.

## Purpose

Specify when backing must NOT be reclaimed during a native driver port. This is
original, single-threaded, bounded metadata code. It does not implement XNU VM,
Linux GEM, DMA, an allocator, a GPU driver, or a concurrency model. It is not
ready to place inside a kernel without a reviewed adapter and synchronization.

## State and transitions

A domain has a nonzero session ID and monotonically increasing buffer/lease
serial numbers. Buffer and lease slots can be reused, but old serials must never
be accepted. The caller must not reinitialize a live domain or reuse a session
while old tokens survive. Serial overflow fails closed.

A buffer has an owner and zero or more explicitly typed leases:

| Lease | Release prerequisite represented by the API |
|---|---|
| CPU map | Final relevant CPU mapping retired |
| Kernel map | Kernel alias no longer usable |
| Shared reference | Export/import ownership dropped |
| GPU VM binding | Real unmap and required translation invalidation completed |
| In-flight job | The correct real completion observed |
| Firmware ownership | Firmware acknowledged relinquishing this allocation |

`dl_close_owner` never drops leases. A process exit or file close cannot stand
in for job completion, firmware acknowledgement, or the last CPU mapping.

`dl_timeout` marks the domain lost without completing work. A lost domain
rejects new allocations/work and reclamation. Host mappings may be released,
but device leases remain until externally verified quiescence.

`dl_backend_ack_quiescence` is a deliberately explicit **trust boundary**. It
can be called only after a future backend verifies that device/firmware access
has stopped and translations are invalidated. Its increasing sequence number
prevents reordering; it is NOT proof that a reset happened. The current tests
supply that fact synthetically. There is no real reset implementation here.

Quiescence invalidates device leases. It does not discard CPU maps, kernel maps,
shared references, or the owner. Reclaim requires a live metadata slot with no
owner, no remaining lease, and no unresolved domain loss. Reclaim changes only
metadata, never actual memory.

## Important conservative restrictions

The reference model refuses new holds after owner death, even when exported
references survive. A real shared-buffer driver may need a richer owner model.
That extension must preserve the no-premature-reclaim invariant rather than
silently loosen it. Loss quarantines the whole domain, including purely host
buffers, for simplicity. Tables are bounded; capacity failure never evicts
live state. Operations must be serialized by the future backend.

## Tests implemented

Sixteen named test groups cover owner death, rounding and overflow, bounded
capacity, typed release, double release, stale tokens across reuse, cross-session
rejection, out-of-order completion, invalid inputs, timeout quarantine, ordered
quiescence, retained host mappings and 100,000 deterministic adversarial
transitions with invariant checks after every step.

The randomized sequence is not exhaustive model checking. Sanitizers test this
native process, not GPU DMA or firmware behavior. Freestanding compilation
checks source dependencies; compiler runtime helpers may still be emitted.
