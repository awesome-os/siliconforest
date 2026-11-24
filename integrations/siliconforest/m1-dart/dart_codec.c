/* Original implementation; source facts and upstream notices: PROVENANCE.json. */
#include "dart_codec.h"

#define PA_MASK UINT64_C(0x0000000fffffc000)
#define VALID UINT64_C(1)
#define SP_DISABLE UINT64_C(2)
#define NO_WRITE UINT64_C(0x80)
#define NO_READ UINT64_C(0x100)
#define FULL_SUBPAGE UINT64_C(0x000fff0000000000)
#define LEAF_FIXED (VALID | SP_DISABLE | FULL_SUBPAGE)
#define LEAF_ALLOWED (PA_MASK | LEAF_FIXED | NO_READ | NO_WRITE)
#define TTBR_VALID UINT32_C(0x80000000)
#define TTBR_ALLOWED UINT32_C(0x80fffffc)

int dld_range_check(struct dld_range r, uint64_t limit)
{
    if (!limit || !r.size || r.base >= limit || r.size > limit - r.base)
        return DLD_RANGE;
    return DLD_OK;
}

static int aligned_page(uint64_t pa)
{
    if (pa & (DLD_PAGE - 1)) return DLD_ALIGNMENT;
    return dld_range_check((struct dld_range){pa, DLD_PAGE}, DLD_PA_LIMIT);
}

int dld_ttbr_encode(uint64_t pa, uint32_t *word)
{
    int rc;
    if (!word) return DLD_ARGUMENT;
    rc = aligned_page(pa);
    if (rc) return rc;
    *word = TTBR_VALID | (uint32_t)(pa >> 12);
    return DLD_OK;
}

int dld_ttbr_decode(uint32_t word, uint64_t *pa)
{
    if (!pa) return DLD_ARGUMENT;
    if (!word) return DLD_UNMAPPED;
    if (!(word & TTBR_VALID) || (word & ~TTBR_ALLOWED)) return DLD_ENCODING;
    *pa = (uint64_t)(word & ~TTBR_VALID) << 12;
    return DLD_OK;
}

int dld_table_encode(uint64_t pa, uint64_t *word)
{
    int rc;
    if (!word) return DLD_ARGUMENT;
    rc = aligned_page(pa);
    if (rc) return rc;
    *word = pa | VALID;
    return DLD_OK;
}

int dld_table_decode(uint64_t word, uint64_t *pa)
{
    if (!pa) return DLD_ARGUMENT;
    if (!word) return DLD_UNMAPPED;
    if (!(word & VALID) || (word & ~(PA_MASK | VALID))) return DLD_ENCODING;
    *pa = word & PA_MASK;
    return DLD_OK;
}

static int permissions_ok(unsigned p)
{
    return p && !(p & ~(DLD_READ | DLD_WRITE));
}

int dld_leaf_encode(uint64_t pa, unsigned permissions, uint64_t *word)
{
    int rc;
    if (!word || !permissions_ok(permissions)) return DLD_ARGUMENT;
    rc = aligned_page(pa);
    if (rc) return rc;
    *word = pa | LEAF_FIXED | ((permissions & DLD_WRITE) ? 0 : NO_WRITE) |
            ((permissions & DLD_READ) ? 0 : NO_READ);
    return DLD_OK;
}

int dld_leaf_decode(uint64_t word, struct dld_leaf *leaf)
{
    struct dld_leaf decoded;
    if (!leaf) return DLD_ARGUMENT;
    if (!word) return DLD_UNMAPPED;
    if ((word & LEAF_FIXED) != LEAF_FIXED || (word & ~LEAF_ALLOWED))
        return DLD_ENCODING;
    decoded.pa = word & PA_MASK;
    decoded.permissions = ((word & NO_READ) ? 0 : DLD_READ) |
                          ((word & NO_WRITE) ? 0 : DLD_WRITE);
    if (!decoded.permissions) return DLD_ENCODING;
    *leaf = decoded;
    return DLD_OK;
}

int dld_split_iova(uint64_t iova, struct dld_indices *indices)
{
    struct dld_indices v;
    if (!indices) return DLD_ARGUMENT;
    if (iova >= DLD_IOVA_LIMIT) return DLD_RANGE;
    v.root = 0;
    v.l1 = (uint32_t)(iova >> 25);
    v.l2 = (uint32_t)((iova >> 14) & 0x7ff);
    v.offset = (uint32_t)(iova & (DLD_PAGE - 1));
    *indices = v;
    return DLD_OK;
}

int dld_word_load(const uint8_t *bytes, size_t size, uint64_t *word)
{
    uint64_t v = 0;
    if (!bytes || !word || size != 8) return DLD_ARGUMENT;
    for (unsigned j = 0; j < 8; j++) v |= (uint64_t)bytes[j] << (j * 8);
    *word = v;
    return DLD_OK;
}

int dld_word_store(uint64_t word, uint8_t *bytes, size_t size)
{
    if (!bytes || size != 8) return DLD_ARGUMENT;
    for (unsigned j = 0; j < 8; j++) bytes[j] = (uint8_t)(word >> (j * 8));
    return DLD_OK;
}

/* All operands here were checked as nonempty, bounded, nonwrapping ranges. */
static int overlaps(struct dld_range a, struct dld_range b)
{
    return a.base < b.base + b.size && b.base < a.base + a.size;
}

static int contains(struct dld_range a, struct dld_range b)
{
    return b.base >= a.base && b.size <= a.size && b.base - a.base <= a.size - b.size;
}

static int ranges_check(const struct dld_range *ranges, size_t count)
{
    if (count > DLD_MAX_RANGES) return DLD_LIMIT;
    if (count && !ranges) return DLD_ARGUMENT;
    for (size_t i = 0; i < count; i++) {
        if (dld_range_check(ranges[i], DLD_PA_LIMIT)) return DLD_RANGE;
        for (size_t j = 0; j < i; j++)
            if (overlaps(ranges[i], ranges[j])) return DLD_OVERLAP;
    }
    return DLD_OK;
}

static int policy_check(const struct dld_policy *p)
{
    int rc;
    if (!p || !p->ram_count) return DLD_ARGUMENT;
    if (dld_range_check(p->aperture, DLD_IOVA_LIMIT)) return DLD_RANGE;
    if ((p->aperture.base | p->aperture.size) & (DLD_PAGE - 1)) return DLD_ALIGNMENT;
    rc = ranges_check(p->ram, p->ram_count);
    if (rc) return rc;
    return ranges_check(p->reserved, p->reserved_count);
}

static int allowed_page(const struct dld_policy *p, uint64_t pa)
{
    struct dld_range r = {pa, DLD_PAGE};
    int in_ram = 0, rc = aligned_page(pa);
    if (rc) return rc;
    for (size_t i = 0; i < p->ram_count; i++) in_ram |= contains(p->ram[i], r);
    if (!in_ram) return DLD_RANGE;
    for (size_t i = 0; i < p->reserved_count; i++)
        if (overlaps(p->reserved[i], r)) return DLD_OVERLAP;
    return DLD_OK;
}

static size_t find_table(const struct dld_image *image, uint64_t pa)
{
    for (size_t i = 0; i < image->table_count; i++)
        if (image->tables[i].pa == pa) return i;
    return image->table_count;
}

static uint64_t table_word(const struct dld_table *table, size_t index)
{
    uint64_t v = 0;
    (void)dld_word_load(table->bytes + index * 8, 8, &v);
    return v;
}

static int iova_page_allowed(const struct dld_policy *p, uint64_t iova)
{
    struct dld_range r = {iova, DLD_PAGE};
    if (!contains(p->aperture, r)) return DLD_RANGE;
    if (overlaps(r, (struct dld_range){DLD_MSI_PAGE, DLD_PAGE})) return DLD_OVERLAP;
    return DLD_OK;
}

int dld_validate(const struct dld_policy *p, const struct dld_image *im,
                 struct dld_scratch *scratch, struct dld_stats *stats)
{
    struct dld_stats result = {0, 0};
    uint64_t root_pa;
    size_t root;
    int rc;
    if (!im || !scratch || !stats) return DLD_ARGUMENT;
    rc = policy_check(p);
    if (rc) return rc;
    if (im->table_count > DLD_MAX_TABLES) return DLD_LIMIT;
    if (im->table_count && !im->tables) return DLD_ARGUMENT;
    for (size_t i = 1; i < 4; i++) if (im->ttbr[i]) return DLD_TOPOLOGY;
    rc = dld_ttbr_decode(im->ttbr[0], &root_pa);
    if (rc == DLD_UNMAPPED) {
        if (im->table_count) return DLD_TOPOLOGY;
        *stats = result;
        return DLD_OK;
    }
    if (rc) return rc;
    for (size_t i = 0; i < im->table_count; i++) {
        const struct dld_table *t = &im->tables[i];
        if (!t->bytes || t->size != DLD_PAGE) return DLD_ARGUMENT;
        if (t->level != DLD_ROOT && t->level != DLD_LEAF_TABLE) return DLD_TOPOLOGY;
        if (t->level == DLD_ROOT && t->pa != root_pa) return DLD_TOPOLOGY;
        rc = allowed_page(p, t->pa);
        if (rc) return rc;
        for (size_t j = 0; j < i; j++) if (t->pa == im->tables[j].pa) return DLD_OVERLAP;
        scratch->references[i] = 0;
    }
    root = find_table(im, root_pa);
    if (root == im->table_count || im->tables[root].level != DLD_ROOT) return DLD_TOPOLOGY;
    scratch->references[root] = 1;
    for (size_t l1 = 0; l1 < DLD_ENTRIES; l1++) {
        uint64_t word = table_word(&im->tables[root], l1), child_pa;
        size_t child;
        if (!word) continue;
        if (l1 >= 128) return DLD_RANGE;
        rc = dld_table_decode(word, &child_pa);
        if (rc) return rc;
        child = find_table(im, child_pa);
        if (child == im->table_count || im->tables[child].level != DLD_LEAF_TABLE)
            return DLD_TOPOLOGY;
        if (scratch->references[child]) return DLD_OVERLAP;
        scratch->references[child] = 1;
        for (size_t l2 = 0; l2 < DLD_ENTRIES; l2++) {
            struct dld_leaf leaf;
            uint64_t iova = ((uint64_t)l1 << 25) | ((uint64_t)l2 << 14);
            word = table_word(&im->tables[child], l2);
            if (!word) continue;
            rc = dld_leaf_decode(word, &leaf);
            if (rc) return rc;
            rc = iova_page_allowed(p, iova);
            if (rc) return rc;
            rc = allowed_page(p, leaf.pa);
            if (rc) return rc;
            if (find_table(im, leaf.pa) != im->table_count) return DLD_OVERLAP;
            if (result.leaf_count == DLD_MAX_LEAVES) return DLD_LIMIT;
            for (size_t j = 0; j < result.leaf_count; j++)
                if (scratch->leaf_pa[j] == leaf.pa) return DLD_OVERLAP;
            scratch->leaf_pa[result.leaf_count++] = leaf.pa;
        }
    }
    for (size_t i = 0; i < im->table_count; i++)
        if (!scratch->references[i]) return DLD_TOPOLOGY;
    result.table_count = im->table_count;
    *stats = result;
    return DLD_OK;
}

int dld_translate(const struct dld_policy *p, const struct dld_image *im,
                  struct dld_scratch *scratch, uint64_t iova, unsigned access,
                  uint64_t *pa)
{
    struct dld_indices index;
    struct dld_stats stats;
    struct dld_leaf leaf;
    uint64_t root_pa, child_pa;
    size_t root, child;
    int rc;
    if (!pa || !permissions_ok(access)) return DLD_ARGUMENT;
    rc = dld_validate(p, im, scratch, &stats);
    if (rc) return rc;
    rc = dld_split_iova(iova, &index);
    if (rc) return rc;
    rc = iova_page_allowed(p, iova & ~(DLD_PAGE - 1));
    if (rc) return rc;
    rc = dld_ttbr_decode(im->ttbr[0], &root_pa);
    if (rc) return rc;
    root = find_table(im, root_pa);
    rc = dld_table_decode(table_word(&im->tables[root], index.l1), &child_pa);
    if (rc) return rc;
    child = find_table(im, child_pa);
    rc = dld_leaf_decode(table_word(&im->tables[child], index.l2), &leaf);
    if (rc) return rc;
    if ((leaf.permissions & access) != access) return DLD_PERMISSION;
    *pa = leaf.pa + index.offset;
    return DLD_OK;
}

int dld_rid(uint32_t bus, uint32_t device, uint32_t function, uint32_t *rid)
{
    if (!rid) return DLD_ARGUMENT;
    if (bus > 255 || device > 31 || function > 7) return DLD_RANGE;
    *rid = (bus << 8) | (device << 3) | function;
    return DLD_OK;
}

int dld_bindings_check(const struct dld_binding *b, size_t count,
                       uint32_t first_bus, uint32_t last_bus)
{
    if (count > DLD_MAX_BINDINGS) return DLD_LIMIT;
    if ((count && !b) || first_bus > last_bus || last_bus > 255) return DLD_ARGUMENT;
    for (size_t i = 0; i < count; i++) {
        if (b[i].rid > 0xffff || b[i].sid >= 16 || b[i].slot >= 64 ||
            (b[i].rid >> 8) < first_bus || (b[i].rid >> 8) > last_bus) return DLD_RANGE;
        for (size_t j = 0; j < i; j++)
            if (b[i].rid == b[j].rid || b[i].sid == b[j].sid || b[i].slot == b[j].slot)
                return DLD_OVERLAP;
    }
    return DLD_OK;
}
