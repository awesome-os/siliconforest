/* Original host-only tests; no device access or simulated DMA success. */
#include "dart_codec.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define ROOT_PA UINT64_C(0x800000000)
#define CHILD_PA (ROOT_PA + DLD_PAGE)
#define DATA_PA (ROOT_PA + UINT64_C(0x100000))
static uint8_t root_bytes[16384], child_bytes[16384], extra_bytes[16384];
static struct dld_table tables[3];
static struct dld_range ram[2], reserved[2];
static struct dld_policy policy;
static struct dld_image image;
static struct dld_scratch scratch;
static unsigned cases;

static void put(uint8_t *bytes, size_t slot, uint64_t word)
{
    assert(slot < DLD_ENTRIES);
    assert(dld_word_store(word, bytes + 8 * slot, 8) == DLD_OK);
}

static uint64_t leaf_word(uint64_t pa, unsigned permissions)
{
    uint64_t word = 0;
    assert(dld_leaf_encode(pa, permissions, &word) == DLD_OK);
    return word;
}

static void setup(void)
{
    uint64_t word = 0;
    memset(root_bytes, 0, sizeof(root_bytes));
    memset(child_bytes, 0, sizeof(child_bytes));
    memset(extra_bytes, 0, sizeof(extra_bytes));
    memset(&image, 0, sizeof(image));
    memset(&scratch, 0xa5, sizeof(scratch));
    ram[0] = (struct dld_range){ROOT_PA, UINT64_C(0x2000000)};
    reserved[0] = (struct dld_range){0, 0};
    policy = (struct dld_policy){{DLD_APCIE_BASE, DLD_APCIE_SIZE}, ram, 1, reserved, 0};
    tables[0] = (struct dld_table){ROOT_PA, root_bytes, sizeof(root_bytes), DLD_ROOT};
    tables[1] = (struct dld_table){CHILD_PA, child_bytes, sizeof(child_bytes), DLD_LEAF_TABLE};
    tables[2] = (struct dld_table){CHILD_PA + DLD_PAGE, extra_bytes, sizeof(extra_bytes), DLD_LEAF_TABLE};
    image.tables = tables;
    image.table_count = 2;
    assert(dld_ttbr_encode(ROOT_PA, &image.ttbr[0]) == DLD_OK);
    assert(dld_table_encode(CHILD_PA, &word) == DLD_OK);
    put(root_bytes, 0, word);
    put(child_bytes, 64, leaf_word(DATA_PA, DLD_READ | DLD_WRITE));
}

static void reject(int expected)
{
    struct dld_stats stats = {777, 888};
    int rc = dld_validate(&policy, &image, &scratch, &stats);
    assert(rc == expected);
    assert(stats.table_count == 777 && stats.leaf_count == 888);
}

static void codec_vectors(void)
{
    uint64_t word = 91, pa = 92;
    uint32_t ttbr = 93;
    struct dld_leaf leaf = {94, 95};
    assert(dld_ttbr_encode(UINT64_C(0x812340000), &ttbr) == 0 && ttbr == UINT32_C(0x80812340));
    assert(dld_ttbr_decode(ttbr, &pa) == 0 && pa == UINT64_C(0x812340000));
    assert(dld_table_encode(pa, &word) == 0 && word == UINT64_C(0x0000000812340001));
    assert(dld_leaf_encode(pa, 3, &word) == 0 && word == UINT64_C(0x000fff0812340003));
    assert(dld_leaf_encode(pa, DLD_READ, &word) == 0 && word == UINT64_C(0x000fff0812340083));
    assert(dld_leaf_decode(word, &leaf) == 0 && leaf.pa == pa && leaf.permissions == DLD_READ);
    assert(dld_leaf_encode(pa, DLD_WRITE, &word) == 0 && word == UINT64_C(0x000fff0812340103));
    assert(dld_leaf_decode(word, &leaf) == 0 && leaf.permissions == DLD_WRITE);
    cases++;
}

static void codec_bounds(void)
{
    uint64_t word = 81, pa = 82;
    uint32_t ttbr = 83;
    struct dld_leaf leaf = {84, 85};
    assert(dld_leaf_encode(DLD_PA_LIMIT, 3, &word) == DLD_RANGE && word == 81);
    assert(dld_table_encode(UINT64_MAX & ~(DLD_PAGE - 1), &word) == DLD_RANGE);
    assert(dld_ttbr_encode(DLD_PA_LIMIT, &ttbr) == DLD_RANGE && ttbr == 83);
    assert(dld_leaf_encode(DATA_PA + 1, 3, &word) == DLD_ALIGNMENT);
    assert(dld_table_encode(DATA_PA + 0x1000, &word) == DLD_ALIGNMENT);
    assert(dld_leaf_encode(DATA_PA, 0, &word) == DLD_ARGUMENT);
    assert(dld_leaf_encode(DATA_PA, 4, &word) == DLD_ARGUMENT && word == 81);
    assert(dld_leaf_encode(DLD_PA_LIMIT - DLD_PAGE, 3, &word) == 0);
    assert(dld_leaf_decode(word, &leaf) == 0 && leaf.pa == DLD_PA_LIMIT - DLD_PAGE);
    assert(dld_table_encode(0, &word) == 0 && word == 1);
    assert(dld_table_decode(word, &pa) == 0 && pa == 0);
    pa = 87;
    assert(dld_ttbr_decode(0, &pa) == DLD_UNMAPPED && pa == 87);
    assert(dld_table_decode(0, &pa) == DLD_UNMAPPED && pa == 87);
    leaf = (struct dld_leaf){84, 85};
    assert(dld_leaf_decode(0, &leaf) == DLD_UNMAPPED && leaf.pa == 84 && leaf.permissions == 85);
    assert(dld_leaf_decode(2, &leaf) == DLD_ENCODING);
    assert(dld_table_decode(2, &pa) == DLD_ENCODING);
    assert(dld_ttbr_decode(4, &pa) == DLD_ENCODING);
    assert(dld_ttbr_encode(0, &ttbr) == 0 && ttbr == UINT32_C(0x80000000));
    assert(dld_ttbr_decode(ttbr, &pa) == 0 && pa == 0);
    cases++;
}

static void reserved_bits(void)
{
    uint64_t valid = leaf_word(DATA_PA, 3), pa;
    struct dld_leaf leaf;
    for (unsigned bit = 0; bit < 64; bit++) {
        if ((bit >= 2 && bit <= 6) || (bit >= 9 && bit <= 13) || bit >= 36) {
            /* bits40..51 are required all-one subpage-end, not PA bits. */
            assert(dld_leaf_decode(valid ^ (UINT64_C(1) << bit), &leaf) == DLD_ENCODING);
        }
        if (bit != 0 && !(bit >= 14 && bit <= 35))
            assert(dld_table_decode((DATA_PA | 1) | (UINT64_C(1) << bit), &pa) == DLD_ENCODING);
    }
    assert(dld_leaf_decode(valid & ~UINT64_C(2), &leaf) == DLD_ENCODING);
    assert(dld_leaf_decode(valid | 0x180, &leaf) == DLD_ENCODING);
    for (unsigned bit = 0; bit < 32; bit++)
        if (bit < 2 || (bit >= 24 && bit < 31))
            assert(dld_ttbr_decode(UINT32_C(0x80000000) | (UINT32_C(1) << bit), &pa) == DLD_ENCODING);
    cases++;
}

static void endian_and_roundtrips(void)
{
    const uint8_t expected[8] = {3, 0, 0x34, 0x12, 8, 0xff, 0x0f, 0};
    uint8_t b[10] = {0};
    uint64_t word = 0, seed = UINT64_C(0x51340298);
    assert(dld_word_store(UINT64_C(0x000fff0812340003), b + 1, 8) == 0);
    assert(memcmp(b + 1, expected, 8) == 0 && b[0] == 0 && b[9] == 0);
    assert(dld_word_load(b + 1, 8, &word) == 0 && word == UINT64_C(0x000fff0812340003));
    assert(dld_word_load(b, 7, &word) == DLD_ARGUMENT);
    assert(dld_word_store(3, b, 9) == DLD_ARGUMENT && b[0] == 0);
    for (unsigned i = 0; i < 4096; i++) {
        struct dld_leaf leaf;
        uint64_t pa;
        seed = seed * UINT64_C(6364136223846793005) + 1;
        pa = seed & UINT64_C(0xfffffc000);
        assert(dld_leaf_encode(pa, (i % 3) + 1, &word) == 0);
        assert(dld_word_store(word, b + 1, 8) == 0);
        assert(dld_word_load(b + 1, 8, &word) == 0);
        assert(dld_leaf_decode(word, &leaf) == 0 && leaf.pa == pa && leaf.permissions == (i % 3) + 1);
    }
    cases++;
}

static void index_and_ranges(void)
{
    struct dld_indices i = {9,9,9,9};
    assert(dld_split_iova(UINT64_C(0xffffffff), &i) == 0);
    assert(i.root == 0 && i.l1 == 127 && i.l2 == 2047 && i.offset == 16383);
    assert(dld_split_iova(UINT64_C(0x2000000), &i) == 0 && i.l1 == 1 && !i.l2 && !i.offset);
    assert(dld_split_iova(DLD_IOVA_LIMIT, &i) == DLD_RANGE && i.l1 == 1);
    assert(dld_split_iova(UINT64_MAX, &i) == DLD_RANGE);
    assert(dld_range_check((struct dld_range){0, DLD_PA_LIMIT}, DLD_PA_LIMIT) == 0);
    assert(dld_range_check((struct dld_range){DLD_PA_LIMIT-1,1}, DLD_PA_LIMIT) == 0);
    assert(dld_range_check((struct dld_range){DLD_PA_LIMIT-1,2}, DLD_PA_LIMIT) == DLD_RANGE);
    assert(dld_range_check((struct dld_range){UINT64_MAX-7,16}, UINT64_MAX) == DLD_RANGE);
    assert(dld_range_check((struct dld_range){0,0}, DLD_PA_LIMIT) == DLD_RANGE);
    cases++;
}

static void valid_walk(void)
{
    struct dld_stats s;
    uint64_t pa = 999;
    setup();
    assert(dld_validate(&policy, &image, &scratch, &s) == 0 && s.table_count == 2 && s.leaf_count == 1);
    assert(dld_translate(&policy, &image, &scratch, DLD_APCIE_BASE+0x3fff, DLD_READ, &pa) == 0);
    assert(pa == DATA_PA+0x3fff);
    pa = 999;
    assert(dld_translate(&policy, &image, &scratch, DLD_APCIE_BASE+DLD_PAGE, DLD_READ, &pa) == DLD_UNMAPPED && pa == 999);
    assert(dld_translate(&policy, &image, &scratch, UINT64_C(0x2000000), DLD_READ, &pa) == DLD_UNMAPPED);
    put(child_bytes, 64, leaf_word(DATA_PA, DLD_READ));
    assert(dld_translate(&policy, &image, &scratch, DLD_APCIE_BASE, DLD_WRITE, &pa) == DLD_PERMISSION && pa == 999);
    assert(dld_translate(&policy, &image, &scratch, DLD_APCIE_BASE, 3, &pa) == DLD_PERMISSION);
    assert(dld_translate(&policy, &image, &scratch, 0, DLD_READ, &pa) == DLD_RANGE);
    image.ttbr[0] = 0; image.table_count = 0;
    assert(dld_validate(&policy, &image, &scratch, &s) == 0 && !s.table_count && !s.leaf_count);
    assert(dld_translate(&policy, &image, &scratch, DLD_APCIE_BASE, DLD_READ, &pa) == DLD_UNMAPPED);
    cases++;
}

static void aperture_and_msi(void)
{
    uint64_t t;
    struct dld_stats s;
    setup();put(child_bytes,63,leaf_word(DATA_PA+DLD_PAGE,3));reject(DLD_RANGE);
    setup();memset(child_bytes,0,sizeof(child_bytes));put(root_bytes,0,0);
    assert(dld_table_encode(CHILD_PA,&t)==0);put(root_bytes,95,t);
    put(child_bytes,1983,leaf_word(DATA_PA,3)); /* last aperture page: 0xbfefc000 */
    assert(dld_validate(&policy,&image,&scratch,&s)==0);
    put(child_bytes,1984,leaf_word(DATA_PA+DLD_PAGE,3));reject(DLD_RANGE);
    setup();policy.aperture=(struct dld_range){0,DLD_IOVA_LIMIT};
    put(root_bytes,0,0);put(root_bytes,127,t);memset(child_bytes,0,sizeof(child_bytes));
    put(child_bytes,2047,leaf_word(DATA_PA,3));reject(DLD_OVERLAP);
    put(child_bytes,2047,0);put(child_bytes,2046,leaf_word(DATA_PA,3));
    assert(dld_validate(&policy,&image,&scratch,&s)==0);
    cases++;
}

static void table_topology(void)
{
    uint64_t t;
    setup();image.ttbr[1]=image.ttbr[0];reject(DLD_TOPOLOGY);
    setup();image.ttbr[0]=0;reject(DLD_TOPOLOGY);
    setup();tables[0].level=DLD_LEAF_TABLE;reject(DLD_TOPOLOGY);
    setup();tables[1].level=DLD_ROOT;reject(DLD_TOPOLOGY);
    setup();image.table_count=3;reject(DLD_TOPOLOGY); /* orphan */
    setup();assert(dld_table_encode(CHILD_PA,&t)==0);put(root_bytes,1,t);reject(DLD_OVERLAP);
    setup();assert(dld_table_encode(ROOT_PA,&t)==0);put(root_bytes,0,t);reject(DLD_TOPOLOGY);
    setup();assert(dld_table_encode(CHILD_PA+DLD_PAGE,&t)==0);put(root_bytes,0,t);reject(DLD_TOPOLOGY);
    setup();assert(dld_table_encode(CHILD_PA,&t)==0);put(root_bytes,0,0);
    memset(child_bytes,0,sizeof(child_bytes));put(root_bytes,128,t);reject(DLD_RANGE);
    setup();tables[1].pa=ROOT_PA;reject(DLD_OVERLAP);
    cases++;
}

static void physical_policy(void)
{
    setup();put(child_bytes,64,leaf_word(ROOT_PA,3));reject(DLD_OVERLAP);
    setup();put(child_bytes,65,leaf_word(DATA_PA,3));reject(DLD_OVERLAP);
    setup();put(child_bytes,64,leaf_word(ROOT_PA+UINT64_C(0x2000000),3));reject(DLD_RANGE);
    setup();reserved[0]=(struct dld_range){DATA_PA+DLD_PAGE-1,1};policy.reserved_count=1;reject(DLD_OVERLAP);
    setup();reserved[0]=(struct dld_range){ROOT_PA,1};policy.reserved_count=1;reject(DLD_OVERLAP);
    setup();ram[0].base++;ram[0].size--;reject(DLD_RANGE); /* partial table page */
    setup();ram[1]=ram[0];policy.ram_count=2;reject(DLD_OVERLAP);
    setup();reserved[0]=(struct dld_range){DATA_PA,1};reserved[1]=reserved[0];policy.reserved_count=2;reject(DLD_OVERLAP);
    setup();ram[0]=(struct dld_range){UINT64_MAX-7,16};reject(DLD_RANGE);
    cases++;
}

static void limits_and_failure_atomicity(void)
{
    struct dld_stats s;
    uint64_t pa=177;
    setup();
    for (size_t i=0;i<DLD_MAX_LEAVES;i++) put(child_bytes,64+i,leaf_word(DATA_PA+i*DLD_PAGE,3));
    assert(dld_validate(&policy,&image,&scratch,&s)==0 && s.leaf_count==DLD_MAX_LEAVES);
    put(child_bytes,64+DLD_MAX_LEAVES,leaf_word(DATA_PA+DLD_MAX_LEAVES*DLD_PAGE,3));reject(DLD_LIMIT);
    setup();image.table_count=DLD_MAX_TABLES+1;reject(DLD_LIMIT);
    setup();policy.ram_count=DLD_MAX_RANGES+1;reject(DLD_LIMIT);
    setup();policy.aperture.size++;reject(DLD_ALIGNMENT);
    setup();policy.aperture=(struct dld_range){DLD_IOVA_LIMIT-DLD_PAGE,DLD_PAGE*2};reject(DLD_RANGE);
    setup();tables[1].size--;reject(DLD_ARGUMENT);
    setup();tables[1].bytes=NULL;reject(DLD_ARGUMENT);
    setup();put(child_bytes,65,4); /* malformed elsewhere invalidates the entire snapshot */
    assert(dld_translate(&policy,&image,&scratch,DLD_APCIE_BASE,DLD_READ,&pa)==DLD_ENCODING && pa==177);
    assert(dld_ttbr_encode(0,NULL)==DLD_ARGUMENT);
    assert(dld_leaf_decode(0,NULL)==DLD_ARGUMENT);
    assert(dld_validate(NULL,&image,&scratch,&s)==DLD_ARGUMENT);
    cases++;
}

static void rid_boundaries(void)
{
    uint32_t rid=55;
    struct dld_binding b[2]={{0x100,1,0},{0x101,2,63}};
    assert(dld_rid(1,0,0,&rid)==0 && rid==0x100);
    assert(dld_rid(255,31,7,&rid)==0 && rid==0xffff);
    assert(dld_rid(256,0,0,&rid)==DLD_RANGE && rid==0xffff);
    assert(dld_rid(1,32,0,&rid)==DLD_RANGE);
    assert(dld_rid(1,0,8,&rid)==DLD_RANGE);
    assert(dld_bindings_check(b,2,1,8)==0);
    b[1]=(struct dld_binding){0x8ff,15,63};assert(dld_bindings_check(b,2,1,8)==0);
    b[1]=(struct dld_binding){0x101,2,63};
    b[1].sid=1;assert(dld_bindings_check(b,2,1,8)==DLD_OVERLAP);
    b[1]=(struct dld_binding){0x100,2,63};assert(dld_bindings_check(b,2,1,8)==DLD_OVERLAP);
    b[1]=(struct dld_binding){0x101,2,0};assert(dld_bindings_check(b,2,1,8)==DLD_OVERLAP);
    b[1]=(struct dld_binding){0x101,16,63};assert(dld_bindings_check(b,2,1,8)==DLD_RANGE);
    b[1]=(struct dld_binding){0x101,15,64};assert(dld_bindings_check(b,2,1,8)==DLD_RANGE);
    b[1]=(struct dld_binding){0x10000,2,63};assert(dld_bindings_check(b,2,1,8)==DLD_RANGE);
    b[1]=(struct dld_binding){0x900,2,63};assert(dld_bindings_check(b,2,1,8)==DLD_RANGE);
    b[1]=(struct dld_binding){0,2,63};assert(dld_bindings_check(b,2,1,8)==DLD_RANGE);
    assert(dld_bindings_check(NULL,0,1,8)==0);
    assert(dld_bindings_check(NULL,1,1,8)==DLD_ARGUMENT);
    assert(dld_bindings_check(b,DLD_MAX_BINDINGS+1,1,8)==DLD_LIMIT);
    cases++;
}

int main(void)
{
    codec_vectors();codec_bounds();reserved_bits();endian_and_roundtrips();
    index_and_ranges();valid_walk();aperture_and_msi();table_topology();
    physical_policy();limits_and_failure_atomicity();rid_boundaries();
    printf("HOST_ONLY_DART_CODEC groups=%u roundtrips=4096 hardware=none\n",cases);
    return 0;
}
