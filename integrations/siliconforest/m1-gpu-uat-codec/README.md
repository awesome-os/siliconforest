# Offline T8103 UAT descriptor foundation, revision001

This original C11 leaf encodes and decodes a **restricted pre-G15 UAT byte
format**, validates a bounded graph of caller-owned immutable byte arrays, and
resolves one virtual byte address against that graph. It has no transport,
callbacks, allocator, hardware memory access, adapter, table installation,
firmware code or startup path. The GPU observer remains on the user's boot hold.

The pinned MIT Omacom/m1n1 reference establishes enough numeric format to build
this slice. See [PROVENANCE.md](PROVENANCE.md) for exact source facts and the
distinction between reference behavior and this leaf's stricter admission rules.

Run the focused offline checks from the repository root:

```sh
python3 -B integrations/puredarwin/m1-gpu-uat-codec/test_codec.py
```

The runner checks the already acquired UAT source against its exact Git object,
runs synthetic tests under Clang ASan/UBSan and optimized `NDEBUG` C, compiles
an AArch64 freestanding relocatable object without executing it, and requires
12 deliberately broken source variants to fail test assertions. Temporary
binaries and mutated sources are outside the leaf and are removed afterward.
`host-validation-001.json` records the final run. `source-validation-001.json`
freezes all other leaf files and records provenance; changing any frozen file
requires a new revision and receipt.

`uat_codec.h` is the complete interface. Calls write only their ordinary host
output objects. All failures preserve output bytes. Input arrays must remain
valid and immutable for the complete call, with output storage disjoint from
inputs. There is no concurrency, kernel lifetime, or saved-validation-token
guarantee. `uat_lookup` validates the entire supplied graph each time, including
branches outside the queried address, before following the requested path.

The graph admits at most 64 table nodes and 16 ranges of each type. The detached
root input is exactly two little-endian TTBR words. L1 input has eight words
(64 bytes); L2/L3 each have 2048 words (16 KiB). L1's uninspected allocation tail
is not asserted zero. Every table address nevertheless requires a complete
aligned 16 KiB allocation inside one table range. Each leaf similarly requires
a complete page inside one data range. Split coverage across adjacent ranges
does not count. Range overflow, overlap within either list, and any overlap
between the table and data lists refuse. Lists may be unordered. Their contents
are caller assertions and are **not evidence of actual RAM, ownership or DMA
authority**. No board memory or MMIO allowlist is synthesized.

Every supplied node must be reached exactly once from the two roots. This
rejects table aliases, cycles, wrong levels, duplicates, dangling pointers and
unreachable nodes. Distinct leaves may alias the same data page; page counts and
`mapped_bytes` count virtual mappings, not unique allocation bytes. A successful
empty graph is allowed. No context/ASID uniqueness or ownership is established.

Addresses use canonical sign extension of a 40-bit VA: low half
`0..0x7fffffffff`, high half `0xffffff8000000000..0xffffffffffffffff`. Raw
compressed upper-half values are rejected instead of silently masked. Nonempty
range validation requires one TTBR half and reports normalized inclusive first
and last offsets plus intersected page count; it neither rounds nor crosses the
canonical hole. The last byte at `UINT64_MAX` is supported without constructing
an overflowing exclusive endpoint. Translation returns a physical numeric byte
address, raw page flag bits, and bytes remaining in that page only.

Descriptor encoders require 16 KiB alignment and a value below `2^48`; that is
the reference's descriptor field width, **not a T8103 physical-memory-size claim**.
Address zero can be represented as a valid descriptor and still needs an
explicit supplied graph/range; it is not confused with a zero-word hole.

The five host groups cover literal bit encodings, every flag bit, reserved bits,
alignment and maxima; canonical VA boundaries and 129,712 index combinations;
valid, sparse and upper-end graphs with failure-output canaries; 400,000 random
descriptor decodes plus 2,000 random graph-word substitutions; and a maximum
64-node graph containing 126,976 leaf entries. The mutation suite includes a
combined shared-table/unreachable-node regression so total visit count cannot
substitute for explicit visited-node tracking.

This is source/host evidence only. It does not build a Darwin bundle or KC, adopt
a firmware table, establish GPU power/reset readiness, authorize MMIO, allocate
DMA memory, perform cache or TLB maintenance, obtain firmware, submit work,
provide acceleration or demonstrate a working GPU. No held observer, historical
source, packet, controller, run or report was changed. Root remains the sole
hardware actor; this leaf creates no hardware candidate.
