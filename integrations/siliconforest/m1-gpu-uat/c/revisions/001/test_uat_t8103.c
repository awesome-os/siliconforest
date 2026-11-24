/* Host checks for the T8103 software codec. Assertions stay enabled. */
#ifdef NDEBUG
#error UAT_TEST_REQUIRES_ASSERTIONS_NDEBUG_FORBIDDEN
#endif
#include "uat_t8103.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint64_t SENT64 = 0xA5A5A5A5A5A5A5A5ull;
static const uint64_t TTBR_ASID7 = 0x0007000080000001ull;
static const uint64_t PAGE_LEAF = 0x00C0000018004443ull;
static const uint64_t TABLE_L2 = 0x10004003ull;
static const uint64_t BLOCK_L2 = 0x00C0000002000441ull;

static struct uat_flags leaf_flags(void)
{
    struct uat_flags flags;
    flags.os = 1;
    flags.uxn = 1;
    flags.pxn = 0;
    flags.ng = 0;
    flags.af = 1;
    flags.sh = 0;
    flags.ap = 1;
    flags.attr = 0;
    return flags;
}

static struct uat_flags zero_flags(void)
{
    struct uat_flags flags;
    memset(&flags, 0, sizeof flags);
    return flags;
}

static uint64_t pack_pte(uint64_t pa, uint64_t type_bit, struct uat_flags flags)
{
    return ((uint64_t)flags.os << 55) | ((uint64_t)flags.uxn << 54) |
        ((uint64_t)flags.pxn << 53) | ((uint64_t)flags.ng << 11) |
        ((uint64_t)flags.af << 10) | ((uint64_t)flags.sh << 8) |
        ((uint64_t)flags.ap << 6) | ((uint64_t)flags.attr << 2) |
        pa | (type_bit << 1) | 1ull;
}

static void poison_decoded(struct uat_decoded *decoded)
{
    memset(decoded, 0x5a, sizeof *decoded);
}

static void expect_word(int status, uint64_t word)
{
    assert(status != UAT_OK);
    assert(word == SENT64);
}

static void test_geometry(void)
{
    struct uat_level level;
    struct uat_level saved;
    uint32_t bytes = 0x11111111u;
    uint32_t shifts[4] = {39u, 36u, 25u, 14u};
    uint32_t entries[4] = {2u, 8u, 2048u, 2048u};
    uint32_t tables[4] = {16u, 64u, 16384u, 16384u};
    uint32_t i;

    assert(uat_format_accepted("t8103") == UAT_OK);
    assert(uat_format_accepted("T8103") == UAT_ERR_FORMAT);
    assert(uat_format_accepted("g13") == UAT_ERR_FORMAT);
    assert(uat_format_accepted("g14") == UAT_ERR_FORMAT);
    assert(uat_format_accepted("g14x") == UAT_ERR_FORMAT);
    assert(uat_format_accepted("g15") == UAT_ERR_FORMAT);
    assert(uat_format_accepted("g15x") == UAT_ERR_FORMAT);
    assert(uat_format_accepted("pre_g15") == UAT_ERR_FORMAT);
    assert(uat_format_accepted("") == UAT_ERR_FORMAT);
    assert(uat_format_accepted("t8103x") == UAT_ERR_FORMAT);
    assert(uat_format_accepted(NULL) == UAT_ERR_ARGUMENT);
    assert(uat_pa_bits() == 40u);
    assert(uat_page_size() == 16384u);
    assert(uat_page_shift() == 14u);
    assert(uat_l0_shift() == 39u);
    assert(uat_descriptor_field_bits() == 48u);
    assert(uat_context_count() == 64u);
    assert(uat_context_stride() == 16u);
    assert(strcmp(uat_evidence(), "software-encoding") == 0);
    assert(strcmp(uat_status_name(UAT_REFUSED), "refused") == 0);
    assert(strcmp(uat_status_name(UAT_ERR_RESERVED), "reserved") == 0);

    for (i = 0; i < 4u; i++) {
        assert(uat_level(i, &level) == UAT_OK);
        assert(level.level == i);
        assert(level.shift == shifts[i]);
        assert(level.entries == entries[i]);
        assert(level.table_bytes == tables[i]);
        assert(level.span == (1ull << shifts[i]));
        bytes = 0x11111111u;
        assert(uat_table_bytes(i, &bytes) == UAT_OK);
        assert(bytes == tables[i]);
    }
    memset(&level, 0x3c, sizeof level);
    memcpy(&saved, &level, sizeof level);
    assert(uat_level(4u, &level) == UAT_ERR_LEVEL);
    assert(memcmp(&level, &saved, sizeof level) == 0);
    assert(uat_level(8u, &level) == UAT_ERR_ADDRESS);
    assert(memcmp(&level, &saved, sizeof level) == 0);
    assert(uat_level(0u, NULL) == UAT_ERR_ARGUMENT);
    bytes = 0x22222222u;
    assert(uat_table_bytes(4u, &bytes) == UAT_ERR_LEVEL);
    assert(bytes == 0x22222222u);
    assert(uat_table_bytes(0u, NULL) == UAT_ERR_ARGUMENT);
}

static void test_address_split(void)
{
    uint64_t folded = SENT64;
    uint32_t index = 0xabcdefu;
    uint32_t offset = 0xabcdefu;
    uint64_t low = (1ull << 39) | 0x1234ull;
    uint64_t extended = (((1ull << 24) - 1ull) << 40) | low;
    uint64_t va = 0x02004010ull;
    uint32_t expect[4] = {0u, 0u, 1u, 1u};
    uint32_t i;

    assert(uat_fold_virtual(low, &folded) == UAT_OK);
    assert(folded == low);
    folded = SENT64;
    assert(uat_fold_virtual(extended, &folded) == UAT_OK);
    assert(folded == low);
    folded = SENT64;
    assert(uat_fold_virtual(UINT64_MAX, &folded) == UAT_OK);
    assert(folded == (1ull << 40) - 1ull);

    folded = SENT64;
    assert(uat_fold_virtual(1ull << 40, &folded) == UAT_ERR_CANONICAL);
    assert(folded == SENT64);
    assert(uat_fold_virtual((((1ull << 24) - 1ull) << 40), &folded) == UAT_ERR_CANONICAL);
    assert(folded == SENT64);
    assert(uat_fold_virtual(0ull, NULL) == UAT_ERR_ARGUMENT);

    index = 0xabcdefu;
    assert(uat_virtual_index(0u, 1ull << 39, &index) == UAT_OK);
    assert(index == 1u);
    assert(uat_virtual_index(1u, 1ull << 39, &index) == UAT_OK);
    assert(index == 0u);
    assert(uat_virtual_index(0u, extended, &index) == UAT_OK);
    assert(index == 1u);
    for (i = 0; i < 4u; i++) {
        assert(uat_virtual_index(i, va, &index) == UAT_OK);
        assert(index == expect[i]);
    }
    index = 0xabcdefu;
    assert(uat_virtual_index(0u, 1ull << 40, &index) == UAT_ERR_CANONICAL);
    assert(index == 0xabcdefu);
    assert(uat_virtual_index(4u, va, &index) == UAT_ERR_LEVEL);
    assert(index == 0xabcdefu);
    assert(uat_virtual_index(8u, va, &index) == UAT_ERR_ADDRESS);
    assert(index == 0xabcdefu);

    offset = 0xabcdefu;
    assert(uat_page_offset(va, &offset) == UAT_OK);
    assert(offset == 0x10u);
    offset = 0xabcdefu;
    assert(uat_page_offset(1ull << 40, &offset) == UAT_ERR_CANONICAL);
    assert(offset == 0xabcdefu);

    offset = 0xabcdefu;
    assert(uat_context_offset(0u, &offset) == UAT_OK);
    assert(offset == 0u);
    assert(uat_context_offset(63u, &offset) == UAT_OK);
    assert(offset == 1008u);
    offset = 0xabcdefu;
    assert(uat_context_offset(64u, &offset) == UAT_ERR_ARGUMENT);
    assert(offset == 0xabcdefu);
    assert(uat_context_offset(0u, NULL) == UAT_ERR_ARGUMENT);
}

static void test_goldens(void)
{
    struct uat_flags leaf = leaf_flags();
    struct uat_flags zero = zero_flags();
    struct uat_decoded decoded;
    uint64_t word = SENT64;
    uint8_t bytes[16];
    uint32_t i;

    assert(pack_pte(0x18004000ull, 1u, leaf) == PAGE_LEAF);
    assert(pack_pte(0x10004000ull, 1u, zero) == TABLE_L2);
    assert(pack_pte(0x02000000ull, 0u, leaf) == BLOCK_L2);

    assert(uat_encode_ttbr(0x80000000ull, 7u, &word) == UAT_OK);
    assert(word == TTBR_ASID7);
    poison_decoded(&decoded);
    assert(uat_decode(0u, word, &decoded) == UAT_OK);
    assert(decoded.kind == UAT_KIND_TABLE);
    assert(decoded.pa == 0x80000000ull);
    assert(decoded.asid == 7u);
    assert(decoded.translated == 1u);
    assert(decoded.raw_output == 0ull);
    assert(decoded.span == 0ull);
    assert(decoded.reserved0 == 0u);

    word = SENT64;
    assert(uat_encode_pte(3u, UAT_KIND_PAGE, 0x18004000ull, &leaf, &word) == UAT_OK);
    assert(word == PAGE_LEAF);
    assert(uat_decode(3u, word, &decoded) == UAT_OK);
    assert(decoded.kind == UAT_KIND_PAGE);
    assert(decoded.pa == 0x18004000ull);
    assert(decoded.translated == 1u);
    assert(decoded.os == 1u && decoded.uxn == 1u && decoded.ap == 1u && decoded.af == 1u);
    assert(decoded.attr == 0u && decoded.pxn == 0u);

    word = SENT64;
    assert(uat_encode_pte(2u, UAT_KIND_TABLE, 0x10004000ull, &zero, &word) == UAT_OK);
    assert(word == TABLE_L2);
    assert(uat_decode(2u, word, &decoded) == UAT_OK);
    assert(decoded.kind == UAT_KIND_TABLE);
    assert(decoded.pa == 0x10004000ull);
    assert(decoded.translated == 1u);

    word = SENT64;
    assert(uat_encode_pte(2u, UAT_KIND_BLOCK, 0x02000000ull, &leaf, &word) == UAT_OK);
    assert(word == BLOCK_L2);
    poison_decoded(&decoded);
    assert(uat_decode(2u, word, &decoded) == UAT_OK);
    assert(decoded.kind == UAT_KIND_BLOCK);
    assert(decoded.pa == 0ull);
    assert(decoded.translated == 0u);
    assert(decoded.raw_output == 0x02000000ull);
    assert(decoded.span == (1ull << 25));
    assert(decoded.os == 1u && decoded.uxn == 1u && decoded.ap == 1u);

    memset(bytes, 0, sizeof bytes);
    assert(uat_store_word(bytes, 16u, 0u, TTBR_ASID7) == UAT_OK);
    assert(memcmp(bytes, "\x01\x00\x00\x80\x00\x00\x07\x00", 8) == 0);
    word = SENT64;
    assert(uat_load_word(bytes, 16u, 0u, &word) == UAT_OK);
    assert(word == TTBR_ASID7);
    word = SENT64;
    assert(uat_load_word(bytes, 16u, 1u, &word) == UAT_OK);
    assert(word == 0ull);
    for (i = 8; i < 16u; i++) bytes[i] = 0xab;
    word = SENT64;
    assert(uat_load_word(bytes, 8u, 1u, &word) == UAT_ERR_LENGTH);
    assert(word == SENT64);
    assert(bytes[8] == 0xab);
    assert(uat_store_word(bytes, 8u, 1u, TTBR_ASID7) == UAT_ERR_LENGTH);
    assert(bytes[8] == 0xab);
    assert(uat_load_word(NULL, 16u, 0u, &word) == UAT_ERR_ARGUMENT);
    assert(word == SENT64);
    assert(uat_store_word(NULL, 16u, 0u, 1ull) == UAT_ERR_ARGUMENT);
}

static void test_errors_leave_output(void)
{
    struct uat_flags leaf = leaf_flags();
    struct uat_flags bad = zero_flags();
    struct uat_decoded decoded;
    struct uat_decoded saved;
    uint64_t word;
    uint64_t top = (1ull << 40) - 16384ull;
    uint64_t reserved_bits[] = {5ull, 12ull, 13ull, 48ull, 49ull, 50ull, 51ull, 52ull, 56ull, 63ull};
    uint32_t i;

    bad.sh = 4u;
    word = SENT64;
    assert(uat_encode_ttbr(1ull << 40, 0u, &word) == UAT_ERR_ADDRESS);
    expect_word(UAT_ERR_ADDRESS, word);
    word = SENT64;
    assert(uat_encode_ttbr((1ull << 40) - 1ull, 0u, &word) == UAT_ERR_ALIGNMENT);
    expect_word(UAT_ERR_ALIGNMENT, word);
    word = SENT64;
    assert(uat_encode_ttbr(0x80000000ull, 1u << 16, &word) == UAT_ERR_ARGUMENT);
    expect_word(UAT_ERR_ARGUMENT, word);
    assert(uat_encode_ttbr(0ull, 0u, NULL) == UAT_ERR_ARGUMENT);
    word = SENT64;
    assert(uat_encode_ttbr(0ull, 65535u, &word) == UAT_OK);
    assert(word == ((65535ull << 48) | 1ull));

    word = SENT64;
    assert(uat_encode_pte(3u, UAT_KIND_PAGE, 0x18004200ull, &leaf, &word) == UAT_ERR_ALIGNMENT);
    expect_word(UAT_ERR_ALIGNMENT, word);
    word = SENT64;
    assert(uat_encode_pte(2u, UAT_KIND_BLOCK, 0x02004000ull, &leaf, &word) == UAT_ERR_ALIGNMENT);
    expect_word(UAT_ERR_ALIGNMENT, word);
    word = SENT64;
    assert(uat_encode_pte(3u, UAT_KIND_PAGE, 1ull << 40, &leaf, &word) == UAT_ERR_ADDRESS);
    expect_word(UAT_ERR_ADDRESS, word);
    word = SENT64;
    assert(uat_encode_pte(3u, UAT_KIND_PAGE, 1ull << 40, &bad, &word) == UAT_ERR_ADDRESS);
    expect_word(UAT_ERR_ADDRESS, word);
    word = SENT64;
    assert(uat_encode_pte(3u, UAT_KIND_PAGE, 0x18004000ull, &bad, &word) == UAT_ERR_ARGUMENT);
    expect_word(UAT_ERR_ARGUMENT, word);
    word = SENT64;
    assert(uat_encode_pte(0u, UAT_KIND_PAGE, 0x80000000ull, &leaf, &word) == UAT_ERR_DESCRIPTOR);
    expect_word(UAT_ERR_DESCRIPTOR, word);
    word = SENT64;
    assert(uat_encode_pte(3u, UAT_KIND_BLOCK, 0x18004000ull, &leaf, &word) == UAT_ERR_DESCRIPTOR);
    expect_word(UAT_ERR_DESCRIPTOR, word);
    word = SENT64;
    assert(uat_encode_pte(3u, UAT_KIND_TABLE, 0x18004000ull, &leaf, &word) == UAT_ERR_DESCRIPTOR);
    expect_word(UAT_ERR_DESCRIPTOR, word);
    word = SENT64;
    assert(uat_encode_pte(2u, UAT_KIND_PAGE, 0x10004000ull, &leaf, &word) == UAT_ERR_DESCRIPTOR);
    expect_word(UAT_ERR_DESCRIPTOR, word);
    word = SENT64;
    assert(uat_encode_pte(4u, UAT_KIND_PAGE, 0x18004000ull, &leaf, &word) == UAT_ERR_LEVEL);
    expect_word(UAT_ERR_LEVEL, word);
    word = SENT64;
    assert(uat_encode_pte(8u, UAT_KIND_PAGE, 0x18004000ull, &leaf, &word) == UAT_ERR_ADDRESS);
    expect_word(UAT_ERR_ADDRESS, word);
    word = SENT64;
    assert(uat_encode_pte(2u, UAT_KIND_EMPTY, 0ull, &leaf, &word) == UAT_ERR_DESCRIPTOR);
    expect_word(UAT_ERR_DESCRIPTOR, word);
    assert(uat_encode_pte(3u, UAT_KIND_PAGE, 0x18004000ull, NULL, &word) == UAT_ERR_ARGUMENT);
    assert(word == SENT64);
    word = SENT64;
    assert(uat_encode_empty(4u, &word) == UAT_ERR_LEVEL);
    expect_word(UAT_ERR_LEVEL, word);
    assert(uat_encode_empty(2u, &word) == UAT_OK);
    assert(word == 0ull);

    word = SENT64;
    assert(uat_encode_pte(3u, UAT_KIND_PAGE, top, &leaf, &word) == UAT_OK);
    assert((word & (((1ull << 34) - 1ull) << 14)) == top);

    poison_decoded(&decoded);
    memcpy(&saved, &decoded, sizeof decoded);
    assert(uat_decode(0u, 0x80000003ull, &decoded) == UAT_ERR_ALIGNMENT);
    assert(memcmp(&decoded, &saved, sizeof decoded) == 0);
    assert(uat_decode(0u, (1ull << 40) | 1ull, &decoded) == UAT_ERR_ADDRESS);
    assert(memcmp(&decoded, &saved, sizeof decoded) == 0);
    assert(uat_decode(3u, 1ull << 10, &decoded) == UAT_ERR_DESCRIPTOR);
    assert(memcmp(&decoded, &saved, sizeof decoded) == 0);
    assert(uat_decode(3u, 0x18004001ull, &decoded) == UAT_ERR_DESCRIPTOR);
    assert(memcmp(&decoded, &saved, sizeof decoded) == 0);
    assert(uat_decode(4u, PAGE_LEAF, &decoded) == UAT_ERR_LEVEL);
    assert(memcmp(&decoded, &saved, sizeof decoded) == 0);
    assert(uat_decode(8u, PAGE_LEAF, &decoded) == UAT_ERR_ADDRESS);
    assert(memcmp(&decoded, &saved, sizeof decoded) == 0);
    assert(uat_decode(2u, 0ull, NULL) == UAT_ERR_ARGUMENT);
    for (i = 0; i < sizeof reserved_bits / sizeof reserved_bits[0]; i++) {
        assert(uat_decode(3u, PAGE_LEAF | (1ull << reserved_bits[i]), &decoded) == UAT_ERR_RESERVED);
        assert(memcmp(&decoded, &saved, sizeof decoded) == 0);
    }
    assert(uat_decode(2u, 0ull, &decoded) == UAT_OK);
    assert(decoded.kind == UAT_KIND_EMPTY);
    assert(decoded.word == 0ull);
    assert(decoded.translated == 0u);
    assert(decoded.pa == 0ull);
}

static void test_flag_space(void)
{
    struct uat_flags flags;
    struct uat_decoded decoded;
    uint32_t count = 0;
    uint32_t os, uxn, pxn, ng, af, sh, ap, attr;

    for (os = 0; os < 2u; os++) {
        for (uxn = 0; uxn < 2u; uxn++) {
            for (pxn = 0; pxn < 2u; pxn++) {
                for (ng = 0; ng < 2u; ng++) {
                    for (af = 0; af < 2u; af++) {
                        for (sh = 0; sh < 4u; sh++) {
                            for (ap = 0; ap < 4u; ap++) {
                                for (attr = 0; attr < 8u; attr++) {
                                    uint64_t word = SENT64;
                                    flags.os = (uint8_t)os;
                                    flags.uxn = (uint8_t)uxn;
                                    flags.pxn = (uint8_t)pxn;
                                    flags.ng = (uint8_t)ng;
                                    flags.af = (uint8_t)af;
                                    flags.sh = (uint8_t)sh;
                                    flags.ap = (uint8_t)ap;
                                    flags.attr = (uint8_t)attr;
                                    assert(uat_encode_pte(3u, UAT_KIND_PAGE, 0x18004000ull,
                                                          &flags, &word) == UAT_OK);
                                    assert(word == pack_pte(0x18004000ull, 1u, flags));
                                    assert(uat_decode(3u, word, &decoded) == UAT_OK);
                                    assert(decoded.kind == UAT_KIND_PAGE);
                                    assert(decoded.pa == 0x18004000ull);
                                    assert(decoded.translated == 1u);
                                    assert(decoded.os == flags.os && decoded.uxn == flags.uxn);
                                    assert(decoded.pxn == flags.pxn && decoded.ng == flags.ng);
                                    assert(decoded.af == flags.af && decoded.sh == flags.sh);
                                    assert(decoded.ap == flags.ap && decoded.attr == flags.attr);
                                    assert(decoded.asid == 0u && decoded.reserved0 == 0u);
                                    count++;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    assert(count == 4096u);
    {
        struct uat_decoded decoded;
        uint64_t word = SENT64;
        assert(uat_encode_ttbr(0ull, 0u, &word) == UAT_OK);
        assert(word == 1ull);
        assert(uat_decode(0u, word, &decoded) == UAT_OK);
        assert(decoded.pa == 0ull);
        assert(decoded.asid == 0u);
        assert(decoded.kind == UAT_KIND_TABLE);
    }
}

static void test_block_refusal(void)
{
    struct uat_block_facts facts;
    struct uat_block_facts saved;
    uint64_t pa = SENT64;
    uint64_t va = 0x02001000ull;

    memset(&facts, 0xa5, sizeof facts);
    assert(uat_refuse_block(2u, va, 0x04000000ull, &pa, &facts) == UAT_REFUSED);
    assert(pa == SENT64);
    assert(facts.raw_output == 0x04000000ull);
    assert(facts.span == (1ull << 25));
    assert(facts.virtual_offset == 0x1000ull);

    pa = SENT64;
    memset(&facts, 0xa5, sizeof facts);
    memcpy(&saved, &facts, sizeof facts);
    assert(uat_refuse_block(2u, 1ull << 40, 0x04000000ull, &pa, &facts) == UAT_ERR_CANONICAL);
    assert(pa == SENT64);
    assert(memcmp(&facts, &saved, sizeof facts) == 0);
    assert(uat_refuse_block(3u, va, 0x18004000ull, &pa, &facts) == UAT_ERR_DESCRIPTOR);
    assert(pa == SENT64);
    assert(memcmp(&facts, &saved, sizeof facts) == 0);
    assert(uat_refuse_block(0u, va, 0x80000000ull, &pa, &facts) == UAT_ERR_DESCRIPTOR);
    assert(memcmp(&facts, &saved, sizeof facts) == 0);
    assert(uat_refuse_block(2u, va, 0x04001000ull, &pa, &facts) == UAT_ERR_ALIGNMENT);
    assert(memcmp(&facts, &saved, sizeof facts) == 0);
    assert(uat_refuse_block(2u, va, 1ull << 40, &pa, &facts) == UAT_ERR_ADDRESS);
    assert(memcmp(&facts, &saved, sizeof facts) == 0);
    assert(uat_refuse_block(4u, va, 0x04000000ull, &pa, &facts) == UAT_ERR_LEVEL);
    assert(memcmp(&facts, &saved, sizeof facts) == 0);
    assert(uat_refuse_block(1u, 0x01000ull, 1ull << 36, &pa, &facts) == UAT_REFUSED);
    assert(pa == SENT64);
    assert(facts.span == (1ull << 36));
    assert(facts.virtual_offset == 0x1000ull);
    memset(&facts, 0xa5, sizeof facts);
    memcpy(&saved, &facts, sizeof facts);
    assert(uat_refuse_block(2u, va, 0x04000000ull, NULL, &facts) == UAT_ERR_ARGUMENT);
    assert(memcmp(&facts, &saved, sizeof facts) == 0);
    assert(uat_refuse_block(2u, va, 0x04000000ull, &pa, NULL) == UAT_ERR_ARGUMENT);
    assert(pa == SENT64);
}

int main(void)
{
    test_geometry();
    test_address_split();
    test_goldens();
    test_errors_leave_output();
    test_flag_space();
    test_block_refusal();
    puts("uat t8103 host checks passed");
    return 0;
}
