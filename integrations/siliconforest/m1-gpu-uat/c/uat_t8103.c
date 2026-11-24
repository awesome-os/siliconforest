#include "uat_t8103.h"

#define UAT_L1_SHIFT 36u
#define UAT_L2_SHIFT 25u
#define UAT_L3_SHIFT 14u
#define UAT_INDEX_BITS 11u
#define UAT_LEVEL_ENTRIES (1u << UAT_INDEX_BITS)
#define UAT_VA_BITS (UAT_L0_SHIFT + 1u)
#define UAT_WORD_BITS 64u

#define UAT_ROLE_TTBR 0u
#define UAT_ROLE_PTE 1u
#define UAT_ROLE_PAGE 2u

/* Bits named by the pinned PTE class. Bit 5, 13:12, 52:48 and 63:56 are not among them. */
#define UAT_PTE_DEFINED ( \
    (1ull << 0) | (1ull << 1) | (7ull << 2) | (3ull << 6) | (3ull << 8) | \
    (1ull << 10) | (1ull << 11) | (((1ull << 34) - 1ull) << 14) | (7ull << 53))

_Static_assert(UAT_PA_BITS == 40u, "acquired output width");
_Static_assert(UAT_L0_SHIFT == 39u, "acquired input width per root");
_Static_assert(UAT_PA_BITS < UAT_DESCRIPTOR_FIELD_BITS, "48-bit field is not the output width");
_Static_assert((1u << (UAT_L0_SHIFT - UAT_L1_SHIFT)) == 8u, "eight level-1 entries");
_Static_assert(UAT_LEVEL_ENTRIES == 2048u, "level entries");
_Static_assert(UAT_PAGE_SIZE == 16384u, "16K granule");
_Static_assert(sizeof(struct uat_decoded) == 48u, "decoded record");
_Static_assert(sizeof(struct uat_flags) == 8u, "flags");
_Static_assert(sizeof(struct uat_block_facts) == 24u, "block facts");

static int pa_ok(uint64_t pa, uint64_t align)
{
    if (pa >= (1ull << UAT_PA_BITS)) return UAT_ERR_ADDRESS;
    if ((pa & (align - 1ull)) != 0ull) return UAT_ERR_ALIGNMENT;
    return UAT_OK;
}

static int flags_ok(const struct uat_flags *flags)
{
    if (flags->os > 1u || flags->uxn > 1u || flags->pxn > 1u || flags->ng > 1u || flags->af > 1u)
        return UAT_ERR_ARGUMENT;
    if (flags->sh > 3u || flags->ap > 3u || flags->attr > 7u) return UAT_ERR_ARGUMENT;
    return UAT_OK;
}

static uint64_t flag_word(const struct uat_flags *flags)
{
    return ((uint64_t)flags->os << 55) | ((uint64_t)flags->uxn << 54) |
        ((uint64_t)flags->pxn << 53) | ((uint64_t)flags->ng << 11) |
        ((uint64_t)flags->af << 10) | ((uint64_t)flags->sh << 8) |
        ((uint64_t)flags->ap << 6) | ((uint64_t)flags->attr << 2);
}

static int spec_of(uint32_t index, uint32_t *shift, uint32_t *entries, uint64_t *span, uint32_t *role)
{
    if (index > 7u) return UAT_ERR_ADDRESS;
    if (index > 3u) return UAT_ERR_LEVEL;
    if (index == 0u) {
        *shift = UAT_L0_SHIFT;
        *entries = UAT_L0_ENTRIES;
        *role = UAT_ROLE_TTBR;
    } else if (index == 1u) {
        *shift = UAT_L1_SHIFT;
        *entries = 1u << (UAT_L0_SHIFT - UAT_L1_SHIFT);
        *role = UAT_ROLE_PTE;
    } else if (index == 2u) {
        *shift = UAT_L2_SHIFT;
        *entries = UAT_LEVEL_ENTRIES;
        *role = UAT_ROLE_PTE;
    } else {
        *shift = UAT_L3_SHIFT;
        *entries = UAT_LEVEL_ENTRIES;
        *role = UAT_ROLE_PAGE;
    }
    *span = 1ull << *shift;
    return UAT_OK;
}

int uat_format_accepted(const char *name)
{
    static const char expect[] = {'t', '8', '1', '0', '3'};
    uint32_t i;
    if (!name) return UAT_ERR_ARGUMENT;
    for (i = 0; i < 5u; i++) if (name[i] != expect[i]) return UAT_ERR_FORMAT;
    if (name[5] != '\0') return UAT_ERR_FORMAT;
    return UAT_OK;
}

uint32_t uat_pa_bits(void) { return UAT_PA_BITS; }
uint32_t uat_page_size(void) { return UAT_PAGE_SIZE; }
uint32_t uat_page_shift(void) { return UAT_PAGE_SHIFT; }
uint32_t uat_l0_shift(void) { return UAT_L0_SHIFT; }
uint32_t uat_descriptor_field_bits(void) { return UAT_DESCRIPTOR_FIELD_BITS; }
uint32_t uat_context_count(void) { return UAT_CONTEXTS; }
uint32_t uat_context_stride(void) { return UAT_CONTEXT_STRIDE; }
const char *uat_evidence(void) { return "software-encoding"; }

const char *uat_status_name(int status)
{
    switch (status) {
    case UAT_OK: return "ok";
    case UAT_ERR_FORMAT: return "format";
    case UAT_ERR_ARGUMENT: return "argument";
    case UAT_ERR_ADDRESS: return "address";
    case UAT_ERR_ALIGNMENT: return "alignment";
    case UAT_ERR_CANONICAL: return "canonical";
    case UAT_ERR_RESERVED: return "reserved";
    case UAT_ERR_DESCRIPTOR: return "descriptor";
    case UAT_ERR_LEVEL: return "level";
    case UAT_ERR_LENGTH: return "length";
    case UAT_REFUSED: return "refused";
    default: return "unknown";
    }
}

int uat_level(uint32_t index, struct uat_level *out)
{
    uint32_t shift, entries, role;
    uint64_t span;
    int status;
    if (!out) return UAT_ERR_ARGUMENT;
    status = spec_of(index, &shift, &entries, &span, &role);
    if (status != UAT_OK) return status;
    (void)role;
    out->level = index;
    out->shift = shift;
    out->entries = entries;
    out->table_bytes = entries * 8u;
    out->span = span;
    return UAT_OK;
}

int uat_table_bytes(uint32_t index, uint32_t *bytes)
{
    struct uat_level level;
    int status;
    if (!bytes) return UAT_ERR_ARGUMENT;
    status = uat_level(index, &level);
    if (status != UAT_OK) return status;
    *bytes = level.table_bytes;
    return UAT_OK;
}

int uat_fold_virtual(uint64_t va, uint64_t *folded)
{
    uint64_t low, top, sign_top;
    if (!folded) return UAT_ERR_ARGUMENT;
    low = va & ((1ull << UAT_VA_BITS) - 1ull);
    top = va >> UAT_VA_BITS;
    if (top == 0ull) {
        *folded = low;
        return UAT_OK;
    }
    sign_top = (1ull << (UAT_WORD_BITS - UAT_VA_BITS)) - 1ull;
    if (((low >> (UAT_VA_BITS - 1u)) & 1ull) == 1ull && top == sign_top) {
        *folded = low;
        return UAT_OK;
    }
    return UAT_ERR_CANONICAL;
}

int uat_virtual_index(uint32_t index, uint64_t va, uint32_t *out)
{
    uint32_t shift, entries, role;
    uint64_t span, folded;
    int status;
    if (!out) return UAT_ERR_ARGUMENT;
    status = spec_of(index, &shift, &entries, &span, &role);
    if (status != UAT_OK) return status;
    (void)role;
    (void)span;
    status = uat_fold_virtual(va, &folded);
    if (status != UAT_OK) return status;
    *out = (uint32_t)((folded >> shift) & (uint64_t)(entries - 1u));
    return UAT_OK;
}

int uat_page_offset(uint64_t va, uint32_t *offset)
{
    uint64_t folded;
    int status;
    if (!offset) return UAT_ERR_ARGUMENT;
    status = uat_fold_virtual(va, &folded);
    if (status != UAT_OK) return status;
    *offset = (uint32_t)(folded & (UAT_PAGE_SIZE - 1u));
    return UAT_OK;
}

int uat_context_offset(uint32_t context, uint32_t *offset)
{
    if (!offset) return UAT_ERR_ARGUMENT;
    if (context >= UAT_CONTEXTS) return UAT_ERR_ARGUMENT;
    *offset = context * UAT_CONTEXT_STRIDE;
    return UAT_OK;
}

int uat_encode_empty(uint32_t level, uint64_t *word)
{
    uint32_t shift, entries, role;
    uint64_t span;
    int status;
    if (!word) return UAT_ERR_ARGUMENT;
    status = spec_of(level, &shift, &entries, &span, &role);
    if (status != UAT_OK) return status;
    (void)role;
    (void)shift;
    (void)entries;
    (void)span;
    *word = 0;
    return UAT_OK;
}

int uat_encode_ttbr(uint64_t pa, uint32_t asid, uint64_t *word)
{
    int status;
    if (!word) return UAT_ERR_ARGUMENT;
    if (asid >= (1u << 16)) return UAT_ERR_ARGUMENT;
    status = pa_ok(pa, UAT_PAGE_SIZE);
    if (status != UAT_OK) return status;
    *word = ((uint64_t)asid << 48) | pa | 1ull;
    return UAT_OK;
}

int uat_encode_pte(uint32_t level, uint32_t kind, uint64_t pa,
                   const struct uat_flags *flags, uint64_t *word)
{
    uint32_t shift, entries, role;
    uint64_t span, align, built;
    int status;
    if (!word || !flags) return UAT_ERR_ARGUMENT;
    status = spec_of(level, &shift, &entries, &span, &role);
    if (status != UAT_OK) return status;
    if (kind != UAT_KIND_TABLE && kind != UAT_KIND_BLOCK && kind != UAT_KIND_PAGE)
        return UAT_ERR_DESCRIPTOR;
    if (role == UAT_ROLE_TTBR || ((kind == UAT_KIND_PAGE) != (role == UAT_ROLE_PAGE)))
        return UAT_ERR_DESCRIPTOR;
    if (kind == UAT_KIND_BLOCK && role != UAT_ROLE_PTE) return UAT_ERR_DESCRIPTOR;
    align = kind == UAT_KIND_BLOCK ? span : (uint64_t)UAT_PAGE_SIZE;
    status = pa_ok(pa, align);
    if (status != UAT_OK) return status;
    status = flags_ok(flags);
    if (status != UAT_OK) return status;
    built = flag_word(flags) | (pa & ((((1ull << 34) - 1ull) << 14))) |
        ((kind == UAT_KIND_BLOCK ? 0ull : 1ull) << 1) | 1ull;
    if ((built & ~UAT_PTE_DEFINED) != 0ull) return UAT_ERR_RESERVED;
    *word = built;
    return UAT_OK;
}

static void clear_decoded(struct uat_decoded *out)
{
    out->word = 0;
    out->pa = 0;
    out->raw_output = 0;
    out->span = 0;
    out->asid = 0;
    out->kind = 0;
    out->level = 0;
    out->os = 0;
    out->uxn = 0;
    out->pxn = 0;
    out->ng = 0;
    out->af = 0;
    out->sh = 0;
    out->ap = 0;
    out->attr = 0;
    out->has_pa = 0;
    out->reserved0 = 0;
}

static void publish(struct uat_decoded *out, const struct uat_decoded *local)
{
    out->word = local->word;
    out->pa = local->pa;
    out->raw_output = local->raw_output;
    out->span = local->span;
    out->asid = local->asid;
    out->kind = local->kind;
    out->level = local->level;
    out->os = local->os;
    out->uxn = local->uxn;
    out->pxn = local->pxn;
    out->ng = local->ng;
    out->af = local->af;
    out->sh = local->sh;
    out->ap = local->ap;
    out->attr = local->attr;
    out->has_pa = local->has_pa;
    out->reserved0 = local->reserved0;
}

int uat_decode(uint32_t level, uint64_t word, struct uat_decoded *out)
{
    uint32_t shift, entries, role;
    uint64_t span, address, align;
    uint32_t type_bit, kind;
    struct uat_decoded local;
    int status;
    if (!out) return UAT_ERR_ARGUMENT;
    status = spec_of(level, &shift, &entries, &span, &role);
    if (status != UAT_OK) return status;
    (void)shift;
    (void)entries;
    clear_decoded(&local);
    local.word = word;
    local.level = (uint8_t)level;
    if (word == 0ull) {
        local.kind = UAT_KIND_EMPTY;
        publish(out, &local);
        return UAT_OK;
    }
    if ((word & 1ull) == 0ull) return UAT_ERR_DESCRIPTOR;
    if (role == UAT_ROLE_TTBR) {
        address = word & (((1ull << 48) - 1ull) & ~1ull);
        status = pa_ok(address, UAT_PAGE_SIZE);
        if (status != UAT_OK) return status;
        local.kind = UAT_KIND_TABLE;
        local.pa = address;
        local.asid = (uint32_t)(word >> 48);
        local.has_pa = 1;
        publish(out, &local);
        return UAT_OK;
    }
    if ((word & ~UAT_PTE_DEFINED) != 0ull) return UAT_ERR_RESERVED;
    type_bit = (uint32_t)((word >> 1) & 1ull);
    if (role == UAT_ROLE_PAGE) {
        if (type_bit != 1u) return UAT_ERR_DESCRIPTOR;
        kind = UAT_KIND_PAGE;
        align = UAT_PAGE_SIZE;
    } else if (type_bit == 0u) {
        kind = UAT_KIND_BLOCK;
        align = span;
    } else {
        kind = UAT_KIND_TABLE;
        align = UAT_PAGE_SIZE;
    }
    address = word & ((((1ull << 34) - 1ull) << 14));
    status = pa_ok(address, align);
    if (status != UAT_OK) return status;
    local.kind = (uint8_t)kind;
    local.os = (uint8_t)((word >> 55) & 1ull);
    local.uxn = (uint8_t)((word >> 54) & 1ull);
    local.pxn = (uint8_t)((word >> 53) & 1ull);
    local.ng = (uint8_t)((word >> 11) & 1ull);
    local.af = (uint8_t)((word >> 10) & 1ull);
    local.sh = (uint8_t)((word >> 8) & 3ull);
    local.ap = (uint8_t)((word >> 6) & 3ull);
    local.attr = (uint8_t)((word >> 2) & 7ull);
    if (kind == UAT_KIND_BLOCK) {
        local.raw_output = address;
        local.span = span;
        local.has_pa = 0;
    } else {
        local.pa = address;
        local.has_pa = 1;
    }
    publish(out, &local);
    return UAT_OK;
}

int uat_load_word(const uint8_t *bytes, uint32_t length, uint32_t index, uint64_t *word)
{
    uint64_t start, value;
    uint32_t i;
    if (!word) return UAT_ERR_ARGUMENT;
    if (!bytes) return UAT_ERR_ARGUMENT;
    start = (uint64_t)index * 8ull;
    if ((uint64_t)length < start + 8ull) return UAT_ERR_LENGTH;
    value = 0;
    for (i = 0; i < 8u; i++) value |= (uint64_t)bytes[start + i] << (8u * i);
    *word = value;
    return UAT_OK;
}

int uat_store_word(uint8_t *bytes, uint32_t length, uint32_t index, uint64_t word)
{
    uint64_t start;
    uint32_t i;
    if (!bytes) return UAT_ERR_ARGUMENT;
    start = (uint64_t)index * 8ull;
    if ((uint64_t)length < start + 8ull) return UAT_ERR_LENGTH;
    for (i = 0; i < 8u; i++) bytes[start + i] = (uint8_t)((word >> (8u * i)) & 0xffu);
    return UAT_OK;
}

int uat_refuse_block(uint32_t level, uint64_t va, uint64_t raw_output,
                     uint64_t *pa, struct uat_block_facts *facts)
{
    uint32_t shift, entries, role;
    uint64_t span, folded;
    int status;
    if (!pa || !facts) return UAT_ERR_ARGUMENT;
    status = spec_of(level, &shift, &entries, &span, &role);
    if (status != UAT_OK) return status;
    (void)shift;
    (void)entries;
    if (role != UAT_ROLE_PTE) return UAT_ERR_DESCRIPTOR;
    status = uat_fold_virtual(va, &folded);
    if (status != UAT_OK) return status;
    status = pa_ok(raw_output, span);
    if (status != UAT_OK) return status;
    facts->raw_output = raw_output;
    facts->span = span;
    facts->virtual_offset = folded & (span - 1ull);
    return UAT_REFUSED;
}
