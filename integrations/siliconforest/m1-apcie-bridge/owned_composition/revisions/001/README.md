# Owned composition: host storage only

This original fixture links the frozen `private_lifecycle` core and the original
fake transport, identity/name lookup, DART, interrupt, ring and opaque item
models. It replaces the old composition's owner Boolean with actual `bpl_claim`,
`bpl_bind_metadata`, `bpl_effect_begin`, `bpl_effect_result` and `bpl_inspect`
operations. The old composition and all predecessor files remain unchanged.

The fixture uses three separate 16 KiB-aligned ordinary host arrays. Numeric
physical addresses and IOVA labels are synthetic; they are never cast into host
pointers. All three metadata extents are 16 KiB; the four-by-eight-byte ring uses
only the data array's 32-byte prefix. The opaque item is exactly the existing
`11 22 33 44 55 66 77 88` fixture, submitted through unchanged `bfi_submit`.
There is no actual firmware message schema, allocation provider, DMA or MMIO.

The healthy synchronous path uses twelve journal entries, below the core's
32-entry limit. It records config (8), mapping (7), descriptor (12), composite
submission (12) and interrupt (16) resource masks before entering the associated
model operation. These numbers are the existing core resource bits. The composite
submission conservatively journals possible data/index/bell effects in one ticket;
it does not claim separate core entries for those internal subevents. Accessor
callbacks check the live registry/slot/generation and current pending ticket.
`pending_kind/resources` and the event array are trace copies of the arguments to
an accepted core operation, not an independent ownership authority.

Each journal result stores the raw model return separately from the core journal
return. In particular, a successful callback followed by UNKNOWN is not success,
and a failed callback's original code is not replaced with `BPL_RETAINED` in the
raw field. Read-only failures outside a begun model operation create no fictional
journal result; earlier exposure still prevents retirement. The test's fresh
fixture reset isolates independent cases and is not a runtime recovery path.

A fake consumer-index advance is explicitly labeled host stimulus. Logical ring
and interrupt close leaves the canonical lease, backing arrays and fake map
retained. New RID/SID/vector conflicts remain refused. There is no free, unload,
retry or cleanup-recovery authority after exposure or uncertainty.

The genuine `native_backing` provider is **not linked or executed**. Its public
API does not expose a usable lease/backing view or admit effects. No manager field
is inspected to bypass that boundary, no second registry is created for a native
owner, and no ACTIVE state or success stub is added. A native join remains blocked
pending a separately reviewed API. Host success is not target, PHY, link, firmware,
DART, MSI, CPU-control protection or physical-ownership proof.

Run from the repository root:

```
python3 integrations/puredarwin/m1-apcie-bridge/owned_composition/test_owned.py
```

The runner uses ASan/UBSan and warnings as errors. It checks the healthy normal/DAR
paths, exact event masks, conflicts after close, private pre-effect retirement,
unknown outcome, late PHY and link loss after index publication, full ring,
32-entry exhaustion, stale result, and every callback failure before/after write
application (287 positions, 574 cases). It also compiles and executes default and
KERNEL-gated paths, refuses host-fake+KERNEL and NDEBUG builds, and counts thirteen
mutants only after successful compilation and an assertion failure. Mutants alter
only this new orchestrator, never frozen core/model code.

Shared PHY/PMGR, PERST GPIO and radio/SMC writers remain compiled out. No target,
network, SSH, firmware, native SDK or hardware operation occurs here.
