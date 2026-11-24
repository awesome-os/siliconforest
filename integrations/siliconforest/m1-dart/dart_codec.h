/* Original darwin-lab software-only codec. See PROVENANCE.json; license pending. */
#ifndef DARWIN_LAB_DART_CODEC_H
#define DARWIN_LAB_DART_CODEC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DLD_PAGE UINT64_C(0x4000)
#define DLD_ENTRIES 2048u
#define DLD_IOVA_LIMIT UINT64_C(0x100000000)
#define DLD_PA_LIMIT UINT64_C(0x1000000000)
#define DLD_APCIE_BASE UINT64_C(0x00100000)
#define DLD_APCIE_SIZE UINT64_C(0xbfe00000)
#define DLD_MSI_PAGE UINT64_C(0xffffc000)
#define DLD_MAX_TABLES 129u
#define DLD_MAX_LEAVES 256u
#define DLD_MAX_RANGES 64u
#define DLD_MAX_BINDINGS 64u
#define DLD_READ 1u
#define DLD_WRITE 2u

enum dld_status {
    DLD_OK = 0, DLD_UNMAPPED = 1,
    DLD_ARGUMENT = -1, DLD_RANGE = -2, DLD_ALIGNMENT = -3,
    DLD_ENCODING = -4, DLD_OVERLAP = -5, DLD_LIMIT = -6,
    DLD_TOPOLOGY = -7, DLD_PERMISSION = -8
};
enum dld_level { DLD_ROOT = 1, DLD_LEAF_TABLE = 2 };
struct dld_range { uint64_t base, size; }; /* half-open; size must be nonzero */
struct dld_indices { uint32_t root, l1, l2, offset; };
struct dld_leaf { uint64_t pa; unsigned permissions; };
struct dld_table {
    uint64_t pa;
    const uint8_t *bytes; /* ordinary caller-owned RAM snapshot, never MMIO */
    size_t size;         /* exactly 16K */
    enum dld_level level;
};
struct dld_policy {
    struct dld_range aperture; /* explicit; no silently selected IOVA window */
    const struct dld_range *ram;
    size_t ram_count;
    const struct dld_range *reserved;
    size_t reserved_count;
};
struct dld_image {
    uint32_t ttbr[4]; /* software values only; T8103 16K profile uses slot zero */
    const struct dld_table *tables;
    size_t table_count;
};
struct dld_scratch {
    uint8_t references[DLD_MAX_TABLES];
    uint64_t leaf_pa[DLD_MAX_LEAVES];
};
struct dld_stats { size_t table_count, leaf_count; };
struct dld_binding { uint32_t rid, sid, slot; };

/* Outputs unchanged on error/unmapped. Null output is always an argument error.
 * These accept canonical full-page encodings only, not arbitrary firmware PTEs.
 * Physical zero is representable; policy, not a null sentinel, decides its use.
 */
int dld_ttbr_encode(uint64_t pa, uint32_t *word);
int dld_ttbr_decode(uint32_t word, uint64_t *pa);
int dld_table_encode(uint64_t pa, uint64_t *word);
int dld_table_decode(uint64_t word, uint64_t *pa);
int dld_leaf_encode(uint64_t pa, unsigned permissions, uint64_t *word);
int dld_leaf_decode(uint64_t word, struct dld_leaf *leaf);
int dld_split_iova(uint64_t iova, struct dld_indices *indices);
/* Exact 8-byte little-endian buffer; no alignment or host-endian assumption. */
int dld_word_load(const uint8_t *bytes, size_t size, uint64_t *word);
int dld_word_store(uint64_t word, uint8_t *bytes, size_t size);
int dld_range_check(struct dld_range range, uint64_t limit);

/* Caller serializes and holds all inputs immutable for the call. Scratch may
 * change on failure; stats/pa never do. Scratch, outputs and inputs must not
 * alias. No allocation, MMIO, page publication, DMA or hardware barrier occurs.
 * Refuses aliases, table/data overlap, orphan/shared tables and >256 leaves.
 */
int dld_validate(const struct dld_policy *policy, const struct dld_image *image,
                 struct dld_scratch *scratch, struct dld_stats *stats);
int dld_translate(const struct dld_policy *policy, const struct dld_image *image,
                  struct dld_scratch *scratch, uint64_t iova, unsigned access,
                  uint64_t *pa);
/* Exact-RID, exclusive-stream model only. No RID mask/grouping or allocation.
 * Bus bounds are an explicit caller policy, not discovery or ownership proof.
 */
int dld_rid(uint32_t bus, uint32_t device, uint32_t function, uint32_t *rid);
int dld_bindings_check(const struct dld_binding *bindings, size_t count,
                       uint32_t first_bus, uint32_t last_bus);
#ifdef __cplusplus
}
#endif
#endif
