# Original/prepared source-join facts producer (profile v2)

Host-only, original pure code. It reads no device, makes no proxy call, writes
no target and allocates nothing. Profile
`t8103-original-prepared-subset-source-facts-v2`. Producer 001 is unchanged.

## What it proves

The pinned HV edits the ADT before the prepared base is captured. For the USB0
selection it deletes 17 `/arm-io` subtrees (`setup_adt`). It also removes the
first `__OS_LOG` segment row from each direct `/arm-io` child and each of that
child's children (`load_raw`). `producer.py` takes the ORIGINAL and PREPARED raw
ADTs as separate inputs and extracts both with the frozen inventory. It then
requires PREPARED to be exactly ORIGINAL with only those two edits applied:

- every unchanged inventoried property is byte-exact, header and padding included;
- each rewritten `segment-names`/`segment-ranges` equals the projected value and
  the pinned m1n1 rebuild (zero-padded 32-byte key, u32 size, zero pad to 4);
- DRAM, VRAM and every pmap row are unchanged.

Anything else refuses: a prepared-only row, a changed row, an extra or missing
removal, the second `__OS_LOG` instead of the first, depth or subtree
differences, or swapped origins. The inventoried-property check is not a
full-ADT replay or quiescence proof.

The ORIGINAL must also predate the HV edits. The pinned `setup_adt` runs once in
`HV.init`, before any `load_raw`, and rewrites three `/product` strings. So the
ORIGINAL strings must carry no `m1n1 hypervisor` marker. PREPARED must hold
exactly one application: name and description plus ` on m1n1 hypervisor`, and
soc name `Virtual <soc> on m1n1 hypervisor` or its `cursed` (SPRR emulation)
form. This refuses a prepared or half-prepared tree passed as ORIGINAL, even
with other bytes changed; that case would otherwise look like a valid projection
source and silently drop original rows. It also refuses identical bytes.

The occurrence map is prepared ordinal → original ordinal. A row's identity is
node path + slot in the projected node + whole 32-byte row + label. The map must
be complete, strictly increasing and one-to-one. Together with the removed
rows it must cover every original ordinal exactly once. The same map is reused
for the physical and numerical-remap observations; spans are never matched
independently. Repeated labels and repeated whole rows stay distinct. The
explicit map, DRAM/VRAM and pmap checks after the property comparison are
implied by it; they state the contract and are not counted as mutation kills.

## What it emits

All ORIGINAL pmap, physical and numerical-remap rows, in the original order and
multiplicity. This includes rows the prepared tree lacks, rows below the current
RAM window, zero remaps and opaque IOVA fields. The wire is the unchanged facts
v1 (908e) format. Its six identity slots keep their literal meaning: slot 3 is
the PREPARED base ADT actually handed to the guest. The guest sees the original
rows plus opaque composite class labels only. It cannot inspect either origin
separately; that would need a new wire version.

Caps: 256 records (counting every original row plus up to 4 MCC slots, VRAM and
boot prefix), a relevant physical union of 8 (enforced by the frozen codec),
and a join receipt of 65536 bytes. Each refuses; nothing is truncated, clipped
or merged to fit. For the saved pair, the tight ceiling is 197 records / 7584
bytes, and the actual 3-slot count is 196 / 7552. The design's 209 / 7968 are
safe overestimates, not produced sizes.

## API

```python
measure(base_adt, original_bootargs, mcc_raw, context, *, profile, original_profile,
        original_adt, source_iodev)
produce(base_adt, original_bootargs, native_bootargs, mcc_raw, context, allocation, *,
        profile, original_profile, original_adt, source_iodev)
```

`base_adt` is the PREPARED tree. `source_iodev` must be the exact built-in
`int` 4; a bool, enum member, int subclass or other value refuses, and leaving
it out is a `TypeError` (required keyword). Both calls run
every check. `Measurement` keeps the six v1 fields and adds
`original_adt_sha256`, `source_iodev` and `source_join_receipt`. The caller must
require `produce(...).measurement == measure(...)` before any use. `Bundle`
exposes `original_inventory`, `prepared_inventory`, `projection`, `expected`,
`plan`, `wire`, `measurement` and `metadata()`; the metadata has counts, hashes
and status only.

`source_selection.select(hv, proxy_module)` is the host-boundary helper. It
accepts only `hv.iodev` that *is* `proxy_module.IODEV.USB0`, where `IODEV` is an
IntEnum named `IODEV` with the standard enum metaclass, defined in that very
module object, and with a member table equal to the pinned `proxy.py`. It
converts with `int.__int__` (never a class's own `__int__`) and returns the
built-in 4 together with a bounded witness record. It refuses a plain int, a
bool, USB1 or another member, another enum with value 4, an int subclass, and
a missing or `None` witness. Binding `proxy_module` to the pinned `proxy.py`
bytes stays the caller's job; the witness records the expected hashes with
`source_bytes_verified_here` false. This is trusted source evidence, not
authentication.

## Join receipt (frozen schema 2)

Canonical JSON: `sort_keys`, separators `,` and `:`, ASCII, no NaN, then LF.
Keys: `schema_version` 2, `profile`, `status`, `selection` {`source_iodev`,
`selector`, `hv_source_sha256`}, `context` {`nonce`, `kernel_sha256`,
`ramdisk_sha256`, `original_bootargs_sha256`, `mcc_raw_sha256`}, `original` and
`prepared` {`adt_sha256`, `adt_bytes`, `counts`, `properties_sha256`,
`rows_sha256`}, `projection` {`hv_product_rewrite`, `removed_original_properties`,
`removed_original_segments` [{`ordinal`, `reason`}], `rewritten_prepared_properties`},
`occurrence_map` {`pmap`, `segments`}, `emitted` {`origin`, `counts`, `records`,
`facts_bytes`}, `caps`, `global_exclusion_coverage` false and
`guest_independent_origin_visibility` false. It holds integers, hashes and fixed
strings only: no paths, names, addresses or property bytes. It does not depend
on the final native BootArgs, so measure and produce return the same bytes.
Under the frozen inventory caps it stays far below 65536 bytes (2628 for the
saved pair); the cap path is tested by lowering the limit.

## Class labels

All framing is little-endian: `F(x)` = u64 length + bytes; `P(props)` = u32 count,
then `F(path) F(key) F(serialized)` per property in inventory order. A row is
`(tag:u32, ordinal:u32, flags:u32, auxiliary:u32, base:u64, bytes:u64)`.

- `properties_sha256` = SHA-256(`BCM_SOURCE_PROPERTIES_V2\0` ‖ P(all)).
- `rows_sha256` = SHA-256(`BCM_SOURCE_ROWS_V2\0` ‖ u32 n ‖ per pmap row
  `F(path) u32 slot F(raw)` ‖ u32 m ‖ per segment `F(path) u32 slot u32 ordinal
  F(label) F(raw)`).
- Class 2–5 label = SHA-256(`BCM_DUAL_ORIGIN_CLASS_V2\0` ‖ u32 tag ‖ F(profile)
  ‖ F(SHA-256(receipt)) ‖ for ORIGINAL then PREPARED: F(role) F(raw ADT SHA-256)
  u64 ADT bytes u32 class cardinality P(relevant props) ‖ u32 map length, u32
  each target ‖ u32 emitted count, each emitted row).
- The relevant properties are: class 2, `/defaults` `pmap-io-ranges`; class 3,
  `/vram` `reg`, `device_type` and the root cells; classes 4 and 5, every
  `segment-*`. The map is the pmap map for class 2, empty for 3, and the
  segment map for 4 and 5.
- Class 1 (MCC) and 6 (boot prefix) keep their v1 framing.

## Checks

Run `python3 validate.py --output-dir <new dir>`. The scripts it runs:

- `test_source_selection.py`: the boundary helper with explicitly fake enums,
  plus AST checks that `proxy.py`, `hv/__init__.py` and `adt.py` (pinned at m1n1
  121fcbf3) still contain the modelled IODEV table, enum storage, `setup_adt`
  call order and product rewrite, deletion templates, `__OS_LOG` loops and
  rebuild layout.
- `test_producer.py`: synthetic pairs whose PREPARED tree comes from an
  independent HV model in `fixtures.py`. Covers duplicates, cross-node equal
  rows, shifted slots, every refusal, the product-rewrite relation (including a
  half-prepared capture), a selected node under an eligible nub, an emptying
  removal, 256/257 records, unions of 8/9 (including
  removed rows inside RAM), adjacency, overlap, no inward clipping, receipt bytes
  against `oracle.py`, and labels.
- `test_c_decoder.py`: the unchanged C 908e decoder, built with the 1024-byte
  frame guard, reads the intended rows, the six identities and the labels.
- `test_mutations.py`: 45 compiled faults, each killed by a targeted assertion.
- `audit_saved_pair.py`: builds a host-only synthetic wire from the actual saved
  pair, using synthetic context labels and a re-encoded native BootArgs. It is
  NOT a grade of the saved capture.

Nothing here admits a runtime handoff, a host application, a capture
requalification, a kernel or RAM-disk rebuild, a packet or a boot.
