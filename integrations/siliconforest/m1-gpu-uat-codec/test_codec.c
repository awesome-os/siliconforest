#include "uat_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
#define OK(c) CHECK((c) == UAT_OK)
static size_t checks;

static void put(uint8_t *p, uint64_t value)
{
    unsigned i;
    for (i = 0; i < 8; ++i) p[i] = (uint8_t)(value >> (i * 8));
}

static void descriptors(void)
{
    uint64_t raw, sentinel, p, flags, values[] = {0, 0x4000, 0x123456780000,
        UINT64_C(0x0000ffffffffc000)};
    struct uat_descriptor d, before;
    unsigned level, bit, n;
    for (n = 0; n < sizeof(values)/sizeof(values[0]); ++n) {
        p = values[n];
        OK(uat_encode_root(p, 0xabcd, &raw));
        CHECK(raw == (UINT64_C(0xabcd000000000001) | p));
        OK(uat_decode(0, raw, &d));
        CHECK(d.address == p && d.asid == 0xabcd && d.kind == UAT_ROOT);
        OK(uat_encode_table(p, &raw)); CHECK(raw == (p | 3));
        for (level = 1; level <= 2; ++level) {
            OK(uat_decode(level, raw, &d));
            CHECK(d.address == p && !d.asid && !d.flags && d.kind == UAT_TABLE);
        }
        for (bit = 0; bit <= 64; ++bit) {
            flags = bit == 64 ? UAT_PAGE_FLAGS : UINT64_C(1) << bit;
            raw = sentinel = UINT64_C(0xfedcba9876543210);
            if (flags & ~UAT_PAGE_FLAGS) {
                CHECK(uat_encode_page(p, flags, &raw) == UAT_DESCRIPTOR);
                CHECK(raw == sentinel);
            } else {
                OK(uat_encode_page(p, flags, &raw));
                OK(uat_decode(3, raw, &d));
                CHECK(raw == (p | flags | 3));
                CHECK(d.address == p && d.flags == flags && d.kind == UAT_PAGE);
            }
        }
    }
    /* Literal independent known fields: OS/UXN, AP1, AF, Shared attr2. */
    OK(uat_encode_page(UINT64_C(0x123456780000), UINT64_C(0x00c0000000000448), &raw));
    CHECK(raw == UINT64_C(0x00c012345678044b));
    memset(&before, 0xa5, sizeof(before));
    for (level = 0; level <= 3; ++level) {
        d = before; OK(uat_decode(level, 0, &d)); CHECK(d.kind == UAT_HOLE);
        d = before; CHECK(uat_decode(level, 2, &d) == UAT_DESCRIPTOR);
        CHECK(memcmp(&d, &before, sizeof(d)) == 0);
    }
    for (level = 1; level <= 3; ++level) {
        CHECK(uat_decode(level, 1, &d) == UAT_UNSUPPORTED);
        for (bit = 0; bit < 64; ++bit) {
            uint64_t mask = UINT64_C(1) << bit;
            raw = UINT64_C(0x4003) | mask;
            if (!(mask & UINT64_C(0x00e0ffffffffcfff)) || bit == 5) {
                d = before; CHECK(uat_decode(level, raw, &d) != UAT_OK);
                CHECK(memcmp(&d, &before, sizeof(d)) == 0);
            }
        }
    }
    for (bit = 1; bit < 14; ++bit)
        CHECK(uat_decode(0, 1 | (UINT64_C(1) << bit), &d) == UAT_ADDRESS);
    CHECK(uat_decode(1, UINT64_C(0x4007), &d) == UAT_UNSUPPORTED);
    CHECK(uat_decode(2, UINT64_C(0x4007), &d) == UAT_UNSUPPORTED);
    raw = sentinel = 123;
    CHECK(uat_encode_root(1, 0, &raw) == UAT_ADDRESS && raw == sentinel);
    CHECK(uat_encode_table(UAT_PA_LIMIT, &raw) == UAT_ADDRESS && raw == sentinel);
    CHECK(uat_encode_page(UINT64_MAX, 0, &raw) == UAT_ADDRESS && raw == sentinel);
    CHECK(uat_encode_root(0, 0, NULL) == UAT_ARGUMENT);
    CHECK(uat_encode_table(0, NULL) == UAT_ARGUMENT);
    CHECK(uat_encode_page(0, 0, NULL) == UAT_ARGUMENT);
    CHECK(uat_decode(4, 0, &d) == UAT_ARGUMENT);
    CHECK(uat_decode(0, 0, NULL) == UAT_ARGUMENT);
    ++checks;
}

static void addresses(void)
{
    struct uat_indices v, before;
    struct uat_va_span s, old;
    uint64_t low_max = UINT64_C(0x7fffffffff);
    uint64_t high_min = UINT64_C(0xffffff8000000000);
    unsigned root, a, b, c;
    for (root = 0; root < 2; ++root)
        for (a = 0; a < 8; ++a)
            for (b = 0; b < 2048; b += 17)
                for (c = 0; c < 2048; c += 31) {
                    uint64_t va = (root ? high_min : 0) | ((uint64_t)a << 36) |
                        ((uint64_t)b << 25) | ((uint64_t)c << 14) | 16383;
                    OK(uat_split_va(va, &v));
                    CHECK(v.root == root && v.l1 == a && v.l2 == b && v.l3 == c && v.offset == 16383);
                }
    OK(uat_split_va(UINT64_MAX, &v));
    CHECK(v.root == 1 && v.l1 == 7 && v.l2 == 2047 && v.l3 == 2047 && v.offset == 16383);
    memset(&before, 0xa5, sizeof(before)); v = before;
    CHECK(uat_split_va(low_max + 1, &v) == UAT_ADDRESS);
    CHECK(memcmp(&v, &before, sizeof(v)) == 0);
    CHECK(uat_split_va(UINT64_C(0x10000000000), &v) == UAT_ADDRESS);
    CHECK(uat_split_va(high_min - 1, &v) == UAT_ADDRESS);
    CHECK(uat_split_va(0, NULL) == UAT_ARGUMENT);
    OK(uat_va_range(16383, 2, &s)); CHECK(s.pages == 2 && s.last_offset == 16384);
    OK(uat_va_range(low_max, 1, &s)); CHECK(s.pages == 1);
    OK(uat_va_range(high_min, UINT64_C(1) << 39, &s));
    CHECK(s.last_offset == UINT64_C(0xffffffffff) && s.pages == (UINT64_C(1) << 25));
    OK(uat_va_range(UINT64_MAX, 1, &s)); CHECK(s.pages == 1);
    memset(&old, 0xa5, sizeof(old)); s = old;
    CHECK(uat_va_range(low_max, 2, &s) == UAT_RANGE);
    CHECK(memcmp(&s, &old, sizeof(s)) == 0);
    CHECK(uat_va_range(UINT64_MAX, 2, &s) == UAT_RANGE);
    CHECK(uat_va_range(0, UINT64_MAX, &s) == UAT_RANGE);
    CHECK(uat_va_range(0, 0, &s) == UAT_RANGE);
    CHECK(uat_va_range(low_max+1, 1, &s) == UAT_ADDRESS);
    CHECK(uat_va_range(0, 1, NULL) == UAT_ARGUMENT);
    ++checks;
}

struct fixture {
    uint8_t roots[16], l1[64], l2[16384], l3[16384];
    struct uat_table tables[4];
    struct uat_range tr[2], dr[2];
    struct uat_snapshot s;
};
static void fixture(struct fixture *f)
{
    memset(f, 0, sizeof(*f));
    put(f->roots, UINT64_C(0xbeef000000004001));
    put(f->l1, 0x8003); put(f->l2, 0xc003);
    put(f->l3, UINT64_C(0x00c000000010044b));
    f->tables[0] = (struct uat_table){0x4000, 1, f->l1, sizeof(f->l1)};
    f->tables[1] = (struct uat_table){0x8000, 2, f->l2, sizeof(f->l2)};
    f->tables[2] = (struct uat_table){0xc000, 3, f->l3, sizeof(f->l3)};
    f->tr[0] = (struct uat_range){0x4000, 0xc000};
    f->dr[0] = (struct uat_range){0x100000, 0x8000};
    f->s = (struct uat_snapshot){f->roots, 16, f->tables, 3, f->tr, f->dr, 1, 1};
}
static void refused(struct fixture *f, enum uat_status expected)
{
    struct uat_summary sum, before;
    struct uat_translation translation, old;
    memset(&before, 0xa5, sizeof(before)); sum = before;
    memset(&old, 0xa5, sizeof(old)); translation = old;
    CHECK(uat_validate(&f->s, &sum) == expected);
    CHECK(memcmp(&sum, &before, sizeof(sum)) == 0);
    CHECK(uat_lookup(&f->s, 0, &translation) == expected);
    CHECK(memcmp(&translation, &old, sizeof(old)) == 0);
}

static void graph(void)
{
    struct fixture f;
    struct uat_summary sum;
    struct uat_translation t, old;
    fixture(&f); OK(uat_validate(&f.s, &sum));
    CHECK(sum.tables == 3 && sum.pages == 1 && sum.mapped_bytes == 16384);
    OK(uat_lookup(&f.s, 123, &t));
    CHECK(t.address == 0x10007b && t.bytes_to_page_end == 16261 && t.page_flags == UINT64_C(0x00c0000000000448));
    OK(uat_lookup(&f.s, 16383, &t)); CHECK(t.address == 0x103fff && t.bytes_to_page_end == 1);
    memset(&old, 0xa5, sizeof(old)); t = old;
    CHECK(uat_lookup(&f.s, 16384, &t) == UAT_UNMAPPED);
    CHECK(memcmp(&t, &old, sizeof(t)) == 0);
    CHECK(uat_lookup(&f.s, UINT64_MAX, &t) == UAT_UNMAPPED);
    /* Exercise highest entry at every level and the sign-extended upper root. */
    memset(f.roots, 0, 16); memset(f.l1, 0, 64); memset(f.l2, 0, 16384); memset(f.l3, 0, 16384);
    put(f.roots + 8, 0x4001); put(f.l1 + 7*8, 0x8003);
    put(f.l2 + 2047*8, 0xc003); put(f.l3 + 2047*8, 0x100003);
    OK(uat_lookup(&f.s, UINT64_MAX, &t)); CHECK(t.address == 0x103fff);
    /* A malformed unreachable branch still poisons an otherwise valid lookup. */
    fixture(&f); put(f.l3+8, 2); refused(&f, UAT_DESCRIPTOR);
    fixture(&f); put(f.roots+8, 0x4001); refused(&f, UAT_GRAPH); /* root sharing */
    fixture(&f); put(f.l2+8, 0xc003); refused(&f, UAT_GRAPH); /* table sharing */
    /* Sharing must not hide an unreachable node behind an equal visit count. */
    fixture(&f); put(f.l2+8, 0xc003); f.s.table_count = 4; f.tr[0].size = 0x10000;
    f.tables[3] = (struct uat_table){0x10000, 3, f.l3, sizeof(f.l3)};
    refused(&f, UAT_GRAPH);
    fixture(&f); put(f.l2, 0x4003); refused(&f, UAT_GRAPH); /* cycle */
    fixture(&f); put(f.l2, 0x8003); refused(&f, UAT_GRAPH); /* self cycle */
    fixture(&f); put(f.l1, 0xc003); refused(&f, UAT_GRAPH); /* wrong level */
    fixture(&f); put(f.l2, 0x10003); refused(&f, UAT_GRAPH); /* dangling */
    fixture(&f); put(f.l2, 0); refused(&f, UAT_GRAPH); /* unreachable node */
    fixture(&f); f.tables[2].address = 0x8000; refused(&f, UAT_GRAPH);
    fixture(&f); f.tables[1].byte_count--; refused(&f, UAT_ARGUMENT);
    fixture(&f); f.tables[0].byte_count++; refused(&f, UAT_ARGUMENT);
    fixture(&f); f.tables[0].level = 0; refused(&f, UAT_ARGUMENT);
    fixture(&f); f.tables[1].bytes = NULL; refused(&f, UAT_ARGUMENT);
    fixture(&f); f.tables[0].address++; refused(&f, UAT_ADDRESS);
    fixture(&f); f.s.root_bytes = 15; refused(&f, UAT_ARGUMENT);
    fixture(&f); f.s.roots = NULL; refused(&f, UAT_ARGUMENT);
    fixture(&f); f.s.tables = NULL; refused(&f, UAT_ARGUMENT);
    fixture(&f); f.s.table_count = 65; refused(&f, UAT_ARGUMENT);
    fixture(&f); f.s.table_range_count = 17; refused(&f, UAT_RANGE);
    fixture(&f); f.s.data_ranges = NULL; refused(&f, UAT_RANGE);
    fixture(&f); f.tr[0].size--; refused(&f, UAT_RANGE); /* partial table */
    fixture(&f); f.dr[0].size = 16383; refused(&f, UAT_RANGE); /* partial page */
    fixture(&f); f.dr[0].start++; refused(&f, UAT_RANGE);
    fixture(&f); f.dr[0] = (struct uat_range){UAT_PA_LIMIT-1, 2}; refused(&f, UAT_RANGE);
    fixture(&f); f.dr[0].size = UINT64_MAX; refused(&f, UAT_RANGE);
    fixture(&f); f.dr[0].size = 0; refused(&f, UAT_RANGE);
    fixture(&f); f.dr[0].start = UAT_PA_LIMIT; refused(&f, UAT_RANGE);
    fixture(&f); f.dr[1] = f.dr[0]; f.s.data_range_count = 2; refused(&f, UAT_RANGE);
    fixture(&f); f.dr[0] = f.tr[0]; refused(&f, UAT_RANGE); /* data/table overlap */
    fixture(&f); f.dr[0] = (struct uat_range){0, 0x108000}; refused(&f, UAT_RANGE);
    fixture(&f); put(f.l3, 0x4003); refused(&f, UAT_RANGE); /* map a table as data */
    fixture(&f); put(f.l1, 0x8001); refused(&f, UAT_UNSUPPORTED); /* block */
    fixture(&f); put(f.l2, 0xc007); refused(&f, UAT_UNSUPPORTED); /* table flags */
    fixture(&f); put(f.l3, 0x100023); refused(&f, UAT_DESCRIPTOR); /* unknown bit5 */
    fixture(&f); f.dr[0] = (struct uat_range){UAT_PA_LIMIT-16384, 16384};
    put(f.l3, UINT64_C(0xffffffffc003));
    OK(uat_lookup(&f.s, 16383, &t)); CHECK(t.address == UAT_PA_LIMIT-1);
    fixture(&f); put(f.l3+8, 0x100003); OK(uat_validate(&f.s, &sum)); CHECK(sum.pages == 2); /* allowed data alias */
    memset(f.roots, 0, 16); f.s.table_count = 0; f.s.table_range_count = 0; f.s.data_range_count = 0;
    OK(uat_validate(&f.s, &sum)); CHECK(!sum.tables && !sum.pages);
    CHECK(uat_validate(NULL, &sum) == UAT_ARGUMENT);
    CHECK(uat_validate(&f.s, NULL) == UAT_ARGUMENT);
    CHECK(uat_lookup(&f.s, 0, NULL) == UAT_ARGUMENT);
    ++checks;
}

static uint64_t random_word(uint64_t *state)
{
    *state ^= *state << 13; *state ^= *state >> 7; *state ^= *state << 17;
    return *state;
}
static void randomized(void)
{
    uint64_t state = UINT64_C(0x3103c0decafebeef), raw, encoded;
    struct uat_descriptor d;
    struct fixture f;
    struct uat_summary sum;
    unsigned i, level;
    for (i = 0; i < 100000; ++i) {
        raw = random_word(&state);
        for (level = 0; level <= 3; ++level) {
            if (uat_decode(level, raw, &d) != UAT_OK) continue;
            if (d.kind == UAT_HOLE) CHECK(raw == 0);
            else if (d.kind == UAT_ROOT) { OK(uat_encode_root(d.address, d.asid, &encoded)); CHECK(encoded == raw); }
            else if (d.kind == UAT_TABLE) { OK(uat_encode_table(d.address, &encoded)); CHECK(encoded == raw); }
            else { OK(uat_encode_page(d.address, d.flags, &encoded)); CHECK(encoded == raw); }
        }
    }
    /* Random bytes never direct host reads: only named immutable byte arrays. */
    for (i = 0; i < 2000; ++i) {
        fixture(&f);
        put(f.l3 + (random_word(&state) % 2048)*8, random_word(&state));
        if (uat_validate(&f.s, &sum) == UAT_OK) CHECK(sum.tables == 3 && sum.pages <= 2);
    }
    ++checks;
}

static void maximum_graph(void)
{
    uint8_t roots[16] = {0};
    uint8_t *storage = calloc(UAT_MAX_TABLES, 16384);
    struct uat_table tables[UAT_MAX_TABLES];
    struct uat_range tr = {0x4000, UAT_MAX_TABLES * UAT_PAGE_SIZE};
    struct uat_range dr = {0x200000, UAT_PAGE_SIZE};
    struct uat_snapshot s = {roots, 16, tables, UAT_MAX_TABLES, &tr, &dr, 1, 1};
    struct uat_summary sum;
    size_t i, j;
    CHECK(storage != NULL); put(roots, 0x4001); put(storage, 0x8003);
    for (i = 0; i < UAT_MAX_TABLES; ++i) {
        tables[i] = (struct uat_table){(i+1)*UAT_PAGE_SIZE, i == 0 ? 1 : i == 1 ? 2 : 3,
            storage+i*16384, i == 0 ? 64 : 16384};
        if (i < 2) continue;
        put(storage+16384+(i-2)*8, (i+1)*UAT_PAGE_SIZE+3);
        for (j = 0; j < 2048; ++j) put(storage+i*16384+j*8, 0x200003);
    }
    OK(uat_validate(&s, &sum));
    CHECK(sum.tables == 64 && sum.pages == 62*2048 && sum.mapped_bytes == UINT64_C(2080374784));
    free(storage);
    ++checks;
}

int main(void)
{
    descriptors(); addresses(); graph(); randomized(); maximum_graph();
    printf("PASS: %zu host groups; synthetic bytes only; no runtime qualification\n", checks);
    return 0;
}
