"""Software-only codec for the pinned m1n1 UAT page-table descriptors.

The functions below handle ordinary integers and byte strings. They do not open
a device, program a GPU, perform DMA, invalidate a TLB, or allocate a table.
A returned page address is arithmetic on the caller's bytes. It is not evidence
that hardware translated anything.

Only the acquired T8103 profile is accepted. Level-0 bit 39 selects the root.
Each root translates 39 input bits. A physical address must be below 2^40.
Other generations, including the m1n1 G >= G15 level-0 bit, are not formats.
Their geometry is recorded in PROVENANCE.json and is not executable here.

Levels use 16 KiB granules. Level 0 has two 8-byte entries. Level 1 has 8
entries. Levels 2 and 3 have 2048. A table descriptor continues the walk.
A page translation adds the offset inside that page. A block stops the walk
and is not translated: the pinned walker never adds the low virtual bits to a
block output, so this module returns the raw output and span under those names
and does not invent a pa.
"""

PAGE_SHIFT = 14
PAGE_SIZE = 1 << PAGE_SHIFT
# Bits 47:14 exist in a descriptor. T8103 does not accept that width as its
# output space; pa_bits below is the acquired limit.
DESCRIPTOR_FIELD_BITS = 48
L1_SHIFT = 36
L2_SHIFT = 25
L3_SHIFT = 14
INDEX_BITS = 11
LEVEL_ENTRIES = 1 << INDEX_BITS
L0_ENTRIES = 2
NUM_CONTEXTS = 64
CONTEXT_STRIDE = L0_ENTRIES * 8
WORD_BITS = 64
WORD_BYTES = 8

FORMATS = {
    "t8103": {"l0_shift": 39, "pa_bits": 40, "pa_limit": "acquired uat_oas"},
}

# Names of the 3-bit attribute field. The integers are the encoding.
ATTR_NAMES = ("Normal", "Device", "Shared", "UNK3", "UNK4", "UNK5", "UNK6", "UNK7")
PTE_FIELDS = ("os", "uxn", "pxn", "ng", "af", "sh", "ap", "attr")
EVIDENCE = "software-encoding"


class UatError(ValueError):
    def __init__(self, code, detail):
        self.code = code
        super().__init__(code + ": " + detail)


def _mask(msb, lsb):
    return ((1 << (msb - lsb + 1)) - 1) << lsb


PTE_DEFINED = (
    _mask(0, 0) | _mask(1, 1) | _mask(4, 2) | _mask(7, 6) | _mask(9, 8)
    | _mask(10, 10) | _mask(11, 11) | _mask(47, 14) | _mask(55, 53)
)
PTE_RESERVED = ((1 << WORD_BITS) - 1) ^ PTE_DEFINED


def _geometry_ok():
    if set(FORMATS) != {"t8103"}:
        return False
    spec = FORMATS["t8103"]
    if spec["l0_shift"] != 39 or spec["pa_bits"] != 40:
        return False
    if spec["pa_limit"] != "acquired uat_oas":
        return False
    if spec["pa_bits"] >= DESCRIPTOR_FIELD_BITS:
        return False
    if L2_SHIFT - L3_SHIFT != INDEX_BITS or L3_SHIFT != PAGE_SHIFT:
        return False
    if LEVEL_ENTRIES != 2048 or PAGE_SIZE != 16384:
        return False
    if (1 << (spec["l0_shift"] - L1_SHIFT)) != 8:
        return False
    defined = PTE_DEFINED
    for bit in (0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 14, 47, 53, 54, 55):
        if (defined >> bit) & 1 != 1:
            return False
    for bit in (5, 12, 13, 48, 52, 56, 63):
        if (defined >> bit) & 1 != 0:
            return False
    return True


if not _geometry_ok():
    raise RuntimeError("UAT geometry constants are inconsistent")


def _format(fmt):
    spec = FORMATS.get(fmt)
    if spec is None:
        raise UatError("format", "unknown UAT format")
    return spec


def _uint(value, what, bits=WORD_BITS):
    if isinstance(value, bool) or not isinstance(value, int):
        raise UatError("argument", what + " is not an integer")
    if value < 0 or value >= (1 << bits):
        raise UatError("address", what + " does not fit in " + str(bits) + " bits")
    return value


def _field(value, width):
    if isinstance(value, bool) or not isinstance(value, int):
        raise UatError("argument", "descriptor field is not an integer")
    if value < 0 or value >= (1 << width):
        raise UatError("argument", "descriptor field does not fit in " + str(width) + " bits")
    return value


def levels(fmt):
    """Return the four levels as plain dicts. Span is the byte size of one entry."""
    spec = _format(fmt)
    l0 = spec["l0_shift"]
    return (
        {"level": 0, "shift": l0, "entries": L0_ENTRIES, "span": 1 << l0, "role": "ttbr"},
        {"level": 1, "shift": L1_SHIFT, "entries": 1 << (l0 - L1_SHIFT), "span": 1 << L1_SHIFT, "role": "pte"},
        {"level": 2, "shift": L2_SHIFT, "entries": LEVEL_ENTRIES, "span": 1 << L2_SHIFT, "role": "pte"},
        {"level": 3, "shift": L3_SHIFT, "entries": LEVEL_ENTRIES, "span": 1 << L3_SHIFT, "role": "page"},
    )


def level(fmt, index):
    _uint(index, "level", 3)
    if index > 3:
        raise UatError("level", "level is outside 0..3")
    return levels(fmt)[index]


def table_bytes(fmt, index):
    return level(fmt, index)["entries"] * WORD_BYTES


def context_offset(ctx):
    """Byte offset of one context's 16-byte TTBR pair. There are 64 contexts."""
    _uint(ctx, "context", 16)
    if ctx >= NUM_CONTEXTS:
        raise UatError("argument", "context is outside 0..63")
    return ctx * CONTEXT_STRIDE


def fold_virtual(fmt, va):
    """Return the low address bits, accepting an in-width value or a sign extension.

    Bits above the format width must be zero, or all ones when the level-0 bit
    is set. Other patterns are rejected rather than masked.
    """
    spec = _format(fmt)
    _uint(va, "virtual address")
    bits = spec["l0_shift"] + 1
    low = va & ((1 << bits) - 1)
    top = va >> bits
    if top == 0:
        return low
    sign = (low >> (bits - 1)) & 1
    if sign == 1 and top == (1 << (WORD_BITS - bits)) - 1:
        return low
    raise UatError("canonical", "virtual address is outside this format")


def virtual_index(fmt, index, va):
    info = level(fmt, index)
    folded = fold_virtual(fmt, va)
    return (folded >> info["shift"]) & (info["entries"] - 1)


def page_offset(fmt, va):
    return fold_virtual(fmt, va) & (PAGE_SIZE - 1)


def _check_pa(fmt, pa, align):
    spec = _format(fmt)
    _uint(pa, "physical address")
    if pa >= (1 << spec["pa_bits"]):
        raise UatError("address", "physical address is outside this format")
    if pa & (align - 1):
        raise UatError("alignment", "physical address is not aligned to " + hex(align))
    return pa


def _pte_flags(fields):
    if set(fields) != set(PTE_FIELDS):
        raise UatError("argument", "page-table fields must be exactly " + ", ".join(PTE_FIELDS))
    os_ = _field(fields["os"], 1)
    uxn = _field(fields["uxn"], 1)
    pxn = _field(fields["pxn"], 1)
    ng = _field(fields["ng"], 1)
    af = _field(fields["af"], 1)
    sh = _field(fields["sh"], 2)
    ap = _field(fields["ap"], 2)
    attr = _field(fields["attr"], 3)
    word = (
        (os_ << 55) | (uxn << 54) | (pxn << 53) | (ng << 11) | (af << 10)
        | (sh << 8) | (ap << 6) | (attr << 2)
    )
    return word, {
        "os": os_, "uxn": uxn, "pxn": pxn, "ng": ng, "af": af,
        "sh": sh, "ap": ap, "attr": attr, "attr_name": ATTR_NAMES[attr],
    }


def encode_descriptor(fmt, index, kind, pa=None, fields=None):
    """Encode one descriptor. fields is required and exact for the kind."""
    info = level(fmt, index)
    if fields is None:
        fields = {}
    if not isinstance(fields, dict):
        raise UatError("argument", "fields is not a dict")
    if kind == "empty":
        if pa is not None or fields:
            raise UatError("argument", "an empty descriptor has no address or fields")
        return 0
    if kind == "table" and info["role"] == "ttbr":
        if set(fields) != {"asid"}:
            raise UatError("argument", "a level-0 table requires only asid")
        asid = _field(fields["asid"], 16)
        _check_pa(fmt, pa, PAGE_SIZE)
        return (asid << 48) | pa | 1
    if kind not in ("table", "block", "page"):
        raise UatError("descriptor", "unknown descriptor kind")
    if info["role"] == "ttbr" or (kind == "page") != (info["role"] == "page"):
        raise UatError("descriptor", "this level does not encode that kind")
    if kind == "block" and info["role"] != "pte":
        raise UatError("descriptor", "this level does not encode a block")
    align = info["span"] if kind == "block" else PAGE_SIZE
    _check_pa(fmt, pa, align)
    flags, _decoded = _pte_flags(fields)
    type_bit = 0 if kind == "block" else 1
    word = flags | (pa & _mask(47, 14)) | (type_bit << 1) | 1
    if word & PTE_RESERVED:
        raise UatError("reserved", "encoder produced a reserved bit")
    return word


def _bytes_of(blob, what):
    if not isinstance(blob, (bytes, bytearray)):
        raise UatError("argument", what + " is not a bytes object")
    return blob


def load_word(blob, index):
    blob = _bytes_of(blob, "table")
    _uint(index, "entry index", 32)
    start = index * WORD_BYTES
    chunk = blob[start:start + WORD_BYTES]
    if len(chunk) != WORD_BYTES:
        raise UatError("length", "table does not contain that entry")
    word = 0
    for shift, byte in enumerate(chunk):
        word |= byte << (8 * shift)
    return word


def store_word(blob, index, word):
    """Store one little-endian word into a caller-owned bytearray."""
    if not isinstance(blob, bytearray):
        raise UatError("argument", "store target is not a bytearray")
    _uint(word, "descriptor word")
    _uint(index, "entry index", 32)
    start = index * WORD_BYTES
    if start + WORD_BYTES > len(blob):
        raise UatError("length", "table does not contain that entry")
    for shift in range(WORD_BYTES):
        blob[start + shift] = (word >> (8 * shift)) & 0xFF
    return blob


def empty_table(fmt, index):
    return bytearray(table_bytes(fmt, index))


def decode_descriptor(fmt, index, word):
    """Decode one descriptor or reject a word this format does not define."""
    info = level(fmt, index)
    _uint(word, "descriptor word")
    base = {"evidence": EVIDENCE, "format": fmt, "level": index, "word": word}
    if word == 0:
        base["kind"] = "empty"
        return base
    if (word & 1) == 0:
        raise UatError("descriptor", "nonzero descriptor with valid clear")
    if info["role"] == "ttbr":
        pa = word & _mask(47, 0) & ~1
        asid = word >> 48
        _check_pa(fmt, pa, PAGE_SIZE)
        base.update(kind="table", pa=pa, asid=asid)
        return base
    if word & PTE_RESERVED:
        raise UatError("reserved", "descriptor has reserved bits set")
    type_bit = (word >> 1) & 1
    if info["role"] == "page":
        if type_bit != 1:
            raise UatError("descriptor", "level 3 requires a page descriptor")
        kind = "page"
        align = PAGE_SIZE
    elif type_bit == 0:
        kind = "block"
        align = info["span"]
    else:
        kind = "table"
        align = PAGE_SIZE
    pa = (word & _mask(47, 14))
    _check_pa(fmt, pa, align)
    _flags, decoded = _pte_flags({
        "os": (word >> 55) & 1,
        "uxn": (word >> 54) & 1,
        "pxn": (word >> 53) & 1,
        "ng": (word >> 11) & 1,
        "af": (word >> 10) & 1,
        "sh": (word >> 8) & 3,
        "ap": (word >> 6) & 3,
        "attr": (word >> 2) & 7,
    })
    base.update(decoded)
    if kind == "block":
        # The encoded field is a raw output address, not a translated pa.
        base.update(kind=kind, raw_output=pa, span=info["span"])
    else:
        base.update(kind=kind, pa=pa)
    return base


def translate(fmt, va, root, tables):
    """Walk caller-supplied tables. Missing bytes are an error, not an identity map.

    A page result includes pa, the page output plus the offset inside that page.
    A block result has no pa. It names raw_output and span, and virtual_offset
    is the low virtual bits that were not added.
    """
    folded = fold_virtual(fmt, va)
    root = _bytes_of(root, "root")
    if len(root) != table_bytes(fmt, 0):
        raise UatError("length", "level-0 record is not 16 bytes")
    if not isinstance(tables, dict):
        raise UatError("argument", "tables is not a dict")
    blob = root
    for info in levels(fmt):
        idx = (folded >> info["shift"]) & (info["entries"] - 1)
        if len(blob) != info["entries"] * WORD_BYTES:
            raise UatError("length", "table has the wrong size for its level")
        decoded = decode_descriptor(fmt, info["level"], load_word(blob, idx))
        if decoded["kind"] == "empty":
            return {
                "evidence": EVIDENCE, "format": fmt, "kind": "unmapped",
                "level": info["level"], "index": idx, "virtual": folded,
            }
        if decoded["kind"] == "table" and info["level"] < 3:
            nxt = tables.get(decoded["pa"])
            if nxt is None:
                raise UatError("missing", "next table was not supplied")
            blob = _bytes_of(nxt, "table")
            continue
        if decoded["kind"] == "page":
            output = decoded["pa"]
            return {
                "evidence": EVIDENCE, "format": fmt, "kind": "page",
                "level": info["level"], "index": idx, "virtual": folded,
                "output": output, "span": info["span"],
                "pa": output + (folded & (PAGE_SIZE - 1)),
                "descriptor": decoded,
            }
        if decoded["kind"] == "block":
            return {
                "evidence": EVIDENCE, "format": fmt, "kind": "block",
                "translation": "refused",
                "level": info["level"], "index": idx, "virtual": folded,
                "raw_output": decoded["raw_output"], "span": info["span"],
                "virtual_offset": folded & (info["span"] - 1),
                "descriptor": decoded,
            }
        raise UatError("descriptor", "walk ended on a table")
    raise UatError("descriptor", "walk did not end")
