# Exact base-ADT source inventory

`extract(base_adt,profile=PROFILE)` accepts immutable supplied ADT bytes and one
explicit profile, returning frozen facts. It uses the existing bounded exact
ADT parser via a fixed source SHA, bounded read and compile of those same bytes.
The private parser API is intentionally bound to that exact source revision.
No device source acquisition, registry, physical access, ADT writer, source
interval union, NPS conversion or native policy/configuration call exists here.
The dependency's source files are read for verification; this is a host-only
extraction function, not a claim of zero filesystem access.

The required unique `/defaults/pmap-io-ranges` property consists of exact24-byte
little-endian `<QQII>` records. Address, byte length, raw wimg and four-character
signature integer remain unchanged. A present empty property is OBSERVED_EMPTY
with its raw property and digest retained; missing is refused. Nonempty partial
records, zero byte extents, overflow or PA36-invalid ranges refuse. Base zero is
valid numeric metadata and implies no absence or physical authority.

Every `segment-ranges` property in the whole tree must have paired
`segment-names`. Ranges are exact32-byte `<QQQII>` observations: phys, opaque IOVA,
numerical remap, byte length, opaque u32. Names use the supported exact ASCII,
semicolon-separated, terminal-NUL format with one nonempty label per slot. No
case folding, trimming, deduplication or interpretation of the opaque fields
occurs. Unknown `segment-*` metadata, dangling pairs, partial records and ambiguous
property/tree identities refuse. Repeated display names and even identical full
rows are distinct observations identified by path and slot. Global source ordinals
follow deterministic sorted path order, with slots in original property order.

IOVA is opaque u64: zero, high values and values that would wrap if wrongly
combined with size remain raw fields. It is never PA-validated, translated or
used in range arithmetic. Physical and numerical remap extents are nonempty and
PA36-bounded. ALL remap observations are retained, including zero, explicitly
labeled ZERO_NUMERICAL_VALUE_NOT_ABSENCE. Remap values are source-only numerical
facts, not physical exclusions or mappings. No alignment/rounding is invented.

`Inventory.dram` comes from exact8-byte `/chosen/dram-base` and `dram-size` values.
`Inventory.vram` is the full declared `/vram/reg` allocation: exactly one `<QQ>`,
`device_type` exactly `vram\0`, root address/size cells exactly2/2, and no root
`ranges`/`dma-ranges` translation metadata. Unsupported shapes or translations
refuse. No BootArgs video geometry is inferred and no MCC containment is required.
A named supplied allocation and a source consumer's reservation behavior are
neither actual Darwin reservation nor backing ownership proof.

Selected properties retain exact payload bytes, exact serialized property bytes
including header/padding, and both recomputed hashes. Raw values/paths belong in
private caller records. `metadata()` emits only counts/digests/scope labels. The
base-ADT digest binds the exact full input; an already-inserted facts property is
refused rather than silently stripped. Source identity hashes do not authenticate
an untrusted caller or prove complete global exclusions.

The independent source capacity is256 projected records: pmap + physical + ALL
remaps + one VRAM. Every record is retained or extraction refuses. This is
separate from the future policy's eight-range union limit. The historical17e
source replay yields109pmap,37physical,37remap (36nonzero,onezero),oneVRAM =184;
no source range/name/raw bytes are printed. This replay is not a new hardware
observation, handoff qualification or old-grade change.

Run `python3 validate.py --output-dir /private/scratch/path` for synthetic tests,
compiled assertion-only mutants and the fixed historical source replay. An
intermediate mutant harness produced assertion failures plus an IndexError in a
new positive-zero-base test; the harness refused that run. Its exact files/log
are preserved separately. Adding a cardinality assertion removed the error-only
kill without changing production semantics; final23mutants all fail assertions.

No fresh source or boot context is acquired. Quiescence, source authenticity,
physical interpretation beyond this schema, global completeness, native policy
and runtime authorization remain separate obligations. Hardware STOP is unchanged.
