/* Original implementation from documented numeric fields; see PROVENANCE.md. */
#include "uat_codec.h"

#define ADDRESS_MASK UINT64_C(0x0000ffffffffc000)
#define VA_MASK UINT64_C(0x000000ffffffffff)
#define HALF_SIZE (UINT64_C(1) << 39)

static int page_address(uint64_t p)
{
    return p < UAT_PA_LIMIT && (p & (UAT_PAGE_SIZE - 1)) == 0;
}

enum uat_status uat_decode(unsigned level, uint64_t raw, struct uat_descriptor *out)
{
    struct uat_descriptor d = {0, 0, 0, UAT_HOLE};
    if (!out || level > 3) return UAT_ARGUMENT;
    if (raw == 0) { *out = d; return UAT_OK; }
    if (!(raw & 1)) return UAT_DESCRIPTOR;
    if (level == 0) {
        /* TTBR BADDR is bits47:1, but this profile requires a 16 KiB table. */
        if (raw & UINT64_C(0x3ffe)) return UAT_ADDRESS;
        d.address = raw & ADDRESS_MASK;
        d.asid = (uint16_t)(raw >> 48);
        d.kind = UAT_ROOT;
    } else {
        if (!(raw & 2)) return UAT_UNSUPPORTED;
        if (raw & ~(ADDRESS_MASK | UAT_PAGE_FLAGS | UINT64_C(3)))
            return UAT_DESCRIPTOR;
        if (level < 3 && (raw & UAT_PAGE_FLAGS)) return UAT_UNSUPPORTED;
        d.address = raw & ADDRESS_MASK;
        d.flags = raw & UAT_PAGE_FLAGS;
        d.kind = level == 3 ? UAT_PAGE : UAT_TABLE;
    }
    *out = d;
    return UAT_OK;
}

enum uat_status uat_encode_root(uint64_t address, uint16_t asid, uint64_t *out)
{
    if (!out) return UAT_ARGUMENT;
    if (!page_address(address)) return UAT_ADDRESS;
    *out = address | ((uint64_t)asid << 48) | UINT64_C(1);
    return UAT_OK;
}

enum uat_status uat_encode_table(uint64_t address, uint64_t *out)
{
    if (!out) return UAT_ARGUMENT;
    if (!page_address(address)) return UAT_ADDRESS;
    *out = address | UINT64_C(3);
    return UAT_OK;
}

enum uat_status uat_encode_page(uint64_t address, uint64_t flags, uint64_t *out)
{
    if (!out) return UAT_ARGUMENT;
    if (!page_address(address)) return UAT_ADDRESS;
    if (flags & ~UAT_PAGE_FLAGS) return UAT_DESCRIPTOR;
    *out = address | flags | UINT64_C(3);
    return UAT_OK;
}

static int canonical(uint64_t va)
{
    uint64_t high = va & ~VA_MASK;
    return (va & HALF_SIZE) ? high == ~VA_MASK : high == 0;
}

enum uat_status uat_split_va(uint64_t va, struct uat_indices *out)
{
    struct uat_indices v;
    if (!out) return UAT_ARGUMENT;
    if (!canonical(va)) return UAT_ADDRESS;
    v.root = (uint16_t)((va >> 39) & 1);
    v.l1 = (uint16_t)((va >> 36) & 7);
    v.l2 = (uint16_t)((va >> 25) & 2047);
    v.l3 = (uint16_t)((va >> 14) & 2047);
    v.offset = (uint16_t)(va & (UAT_PAGE_SIZE - 1));
    *out = v;
    return UAT_OK;
}

enum uat_status uat_va_range(uint64_t va, uint64_t size, struct uat_va_span *out)
{
    struct uat_va_span span;
    if (!out) return UAT_ARGUMENT;
    if (!canonical(va)) return UAT_ADDRESS;
    if (!size || size > HALF_SIZE - (va & (HALF_SIZE - 1))) return UAT_RANGE;
    span.first_offset = va & VA_MASK;
    span.last_offset = span.first_offset + size - 1;
    span.pages = (span.last_offset >> 14) - (span.first_offset >> 14) + 1;
    *out = span;
    return UAT_OK;
}

static uint64_t read_le(const uint8_t *bytes)
{
    uint64_t word = 0;
    unsigned i;
    for (i = 0; i < 8; ++i) word |= (uint64_t)bytes[i] << (i * 8);
    return word;
}

static int overlap(struct uat_range a, struct uat_range b)
{
    /* All callers first validate nonoverflowing ranges below PA_LIMIT. */
    return a.start < b.start + b.size && b.start < a.start + a.size;
}

static int valid_ranges(const struct uat_range *r, size_t n)
{
    size_t i, j;
    if (n > UAT_MAX_RANGES || (n && !r)) return 0;
    for (i = 0; i < n; ++i) {
        if (!r[i].size || r[i].start >= UAT_PA_LIMIT ||
            r[i].size > UAT_PA_LIMIT - r[i].start) return 0;
        for (j = 0; j < i; ++j) if (overlap(r[i], r[j])) return 0;
    }
    return 1;
}

static int contains_page(const struct uat_range *r, size_t n, uint64_t p)
{
    size_t i;
    for (i = 0; i < n; ++i)
        if (p >= r[i].start && p - r[i].start <= r[i].size &&
            UAT_PAGE_SIZE <= r[i].size - (p - r[i].start)) return 1;
    return 0;
}

static size_t find_table(const struct uat_snapshot *s, uint64_t address)
{
    size_t i;
    for (i = 0; i < s->table_count; ++i)
        if (s->tables[i].address == address) return i;
    return s->table_count;
}

static enum uat_status visit(const struct uat_snapshot *s, uint64_t address,
                             unsigned level, uint8_t *seen, struct uat_summary *sum)
{
    size_t index = find_table(s, address), entry, count;
    const struct uat_table *table;
    struct uat_descriptor d;
    enum uat_status status;
    if (index == s->table_count || seen[index]) return UAT_GRAPH;
    table = &s->tables[index];
    if (table->level != level) return UAT_GRAPH;
    seen[index] = 1;
    ++sum->tables;
    count = level == 1 ? 8 : 2048;
    for (entry = 0; entry < count; ++entry) {
        status = uat_decode(level, read_le(table->bytes + entry * 8), &d);
        if (status != UAT_OK) return status;
        if (d.kind == UAT_HOLE) continue;
        if (d.kind == UAT_PAGE) {
            if (!contains_page(s->data_ranges, s->data_range_count, d.address))
                return UAT_RANGE;
            ++sum->pages;
        } else {
            status = visit(s, d.address, level + 1, seen, sum);
            if (status != UAT_OK) return status;
        }
    }
    return UAT_OK;
}

enum uat_status uat_validate(const struct uat_snapshot *s, struct uat_summary *out)
{
    uint8_t seen[UAT_MAX_TABLES] = {0};
    struct uat_summary sum = {0, 0, 0};
    struct uat_descriptor d;
    enum uat_status status;
    size_t i, j;
    if (!s || !out || !s->roots || s->root_bytes != 16 ||
        s->table_count > UAT_MAX_TABLES || (s->table_count && !s->tables))
        return UAT_ARGUMENT;
    if (!valid_ranges(s->table_ranges, s->table_range_count) ||
        !valid_ranges(s->data_ranges, s->data_range_count)) return UAT_RANGE;
    for (i = 0; i < s->table_range_count; ++i)
        for (j = 0; j < s->data_range_count; ++j)
            if (overlap(s->table_ranges[i], s->data_ranges[j])) return UAT_RANGE;
    for (i = 0; i < s->table_count; ++i) {
        const struct uat_table *t = &s->tables[i];
        if (t->level < 1 || t->level > 3 || !t->bytes ||
            t->byte_count != (t->level == 1 ? 64u : 16384u)) return UAT_ARGUMENT;
        if (!page_address(t->address)) return UAT_ADDRESS;
        if (!contains_page(s->table_ranges, s->table_range_count, t->address))
            return UAT_RANGE;
        for (j = 0; j < i; ++j)
            if (s->tables[j].address == t->address) return UAT_GRAPH;
    }
    for (i = 0; i < 2; ++i) {
        status = uat_decode(0, read_le(s->roots + i * 8), &d);
        if (status != UAT_OK) return status;
        if (d.kind == UAT_HOLE) continue;
        status = visit(s, d.address, 1, seen, &sum);
        if (status != UAT_OK) return status;
    }
    if (sum.tables != s->table_count) return UAT_GRAPH;
    sum.mapped_bytes = sum.pages * UAT_PAGE_SIZE;
    *out = sum;
    return UAT_OK;
}

enum uat_status uat_lookup(const struct uat_snapshot *s, uint64_t va,
                           struct uat_translation *out)
{
    struct uat_summary sum;
    struct uat_indices parts;
    struct uat_descriptor d;
    struct uat_translation result;
    unsigned indices[3], level;
    size_t index;
    enum uat_status status;
    if (!out) return UAT_ARGUMENT;
    status = uat_split_va(va, &parts);
    if (status != UAT_OK) return status;
    status = uat_validate(s, &sum);
    if (status != UAT_OK) return status;
    status = uat_decode(0, read_le(s->roots + parts.root * 8), &d);
    if (status != UAT_OK) return status;
    indices[0] = parts.l1; indices[1] = parts.l2; indices[2] = parts.l3;
    for (level = 1; level <= 3; ++level) {
        if (d.kind == UAT_HOLE) return UAT_UNMAPPED;
        index = find_table(s, d.address);
        if (index == s->table_count) return UAT_GRAPH;
        status = uat_decode(level, read_le(s->tables[index].bytes + indices[level-1] * 8), &d);
        if (status != UAT_OK) return status;
    }
    if (d.kind == UAT_HOLE) return UAT_UNMAPPED;
    result.address = d.address + parts.offset;
    result.page_flags = d.flags;
    result.bytes_to_page_end = UAT_PAGE_SIZE - parts.offset;
    *out = result;
    return UAT_OK;
}
