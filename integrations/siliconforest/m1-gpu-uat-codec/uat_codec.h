/* Original offline codec. Original-code license selection remains pending. */
#ifndef DL_UAT_CODEC_H
#define DL_UAT_CODEC_H
#include <stddef.h>
#include <stdint.h>

#define UAT_PAGE_SIZE UINT64_C(16384)
#define UAT_PA_LIMIT (UINT64_C(1) << 48)
#define UAT_MAX_TABLES 64u
#define UAT_MAX_RANGES 16u
/* Numeric fields only; these are not permission or coherency guarantees. */
#define UAT_PAGE_FLAGS UINT64_C(0x00e0000000000fdc)

enum uat_status {
    UAT_OK = 0, UAT_ARGUMENT, UAT_ADDRESS, UAT_DESCRIPTOR, UAT_UNSUPPORTED,
    UAT_RANGE, UAT_GRAPH, UAT_UNMAPPED
};
enum uat_kind { UAT_HOLE = 0, UAT_ROOT, UAT_TABLE, UAT_PAGE };
struct uat_descriptor {
    uint64_t address, flags;
    uint16_t asid;
    enum uat_kind kind;
};
struct uat_indices { uint16_t root, l1, l2, l3, offset; };
struct uat_va_span { uint64_t first_offset, last_offset, pages; };
struct uat_range { uint64_t start, size; };
struct uat_table {
    uint64_t address;
    unsigned level; /* 1: eight entries; 2/3: 2048 entries */
    const uint8_t *bytes;
    size_t byte_count;
};
struct uat_snapshot {
    const uint8_t *roots; /* Exactly two little-endian TTBR words. */
    size_t root_bytes;
    const struct uat_table *tables;
    size_t table_count;
    const struct uat_range *table_ranges, *data_ranges;
    size_t table_range_count, data_range_count;
};
struct uat_summary { uint64_t pages, mapped_bytes; size_t tables; };
struct uat_translation { uint64_t address, page_flags, bytes_to_page_end; };

/* All outputs remain unchanged on failure. Caller supplies ordinary host memory,
 * valid for its declared sizes, immutable for the entire call, with disjoint
 * output storage. No allocation, callbacks, transport, volatile loads or I/O.
 * This strict subset supports zero holes, plain tables, and 16 KiB pages only.
 * Nonzero invalid descriptors and block/table-attribute encodings are refused.
 */
enum uat_status uat_decode(unsigned level, uint64_t raw, struct uat_descriptor *out);
enum uat_status uat_encode_root(uint64_t address, uint16_t asid, uint64_t *out);
enum uat_status uat_encode_table(uint64_t address, uint64_t *out);
enum uat_status uat_encode_page(uint64_t address, uint64_t flags, uint64_t *out);
/* Canonical 40-bit VAs only: bit39 sign-extended through bit63. No masking aliases.
 * Nonempty ranges must remain within one TTBR half, with no rounding or wrapping.
 */
enum uat_status uat_split_va(uint64_t va, struct uat_indices *out);
enum uat_status uat_va_range(uint64_t va, uint64_t size, struct uat_va_span *out);
/* Bounded complete graph: every supplied table is reached exactly once. Cycles,
 * shared/dangling/unreachable tables, wrong levels and duplicate PAs refuse.
 * Full 16 KiB table allocations and data pages must be in separate caller-supplied
 * ranges. L1 snapshots contain only the eight used entries (64 bytes).
 * Page-to-page data aliases are allowed: this is not an ownership proof.
 */
enum uat_status uat_validate(const struct uat_snapshot *, struct uat_summary *out);
/* Revalidates the complete snapshot before resolving one byte address. */
enum uat_status uat_lookup(const struct uat_snapshot *, uint64_t va,
                           struct uat_translation *out);
#endif
