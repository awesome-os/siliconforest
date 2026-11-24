/* Original host-only endpoint map wrapper around the frozen original codec.
 * No IOMapper, DART register access, device DMA or hardware acknowledgement. */
#ifndef DL_BCM4378_FAKE_DART_H
#define DL_BCM4378_FAKE_DART_H
#include "bringup.h"
#include "dart_codec.h"
#ifdef __cplusplus
extern "C" {
#endif

enum bfd_result {
    BFD_FAKE_OK = 0, BFD_GATED = 100, BFD_LINK_DOWN = 101,
    BFD_OWNER = -100, BFD_ARGUMENT = -101, BFD_STATE = -102, BFD_IO = -103,
    BFD_LIMIT = -104
}; /* Other errors are exact DLD_* codec statuses, never coerced to success. */

struct bfd_storage {
    uint8_t *root, *leaf; /* caller-owned ordinary host RAM, exactly16K each */
    size_t root_bytes, leaf_bytes;
    uint64_t root_pa, leaf_pa; /* numeric fake labels, NEVER cast to pointers */
};
struct bfd_workspace {
    uint8_t root[DLD_PAGE], leaf[DLD_PAGE];
    struct dld_scratch scratch;
};
struct bfd_map {
    struct apb_io io;
    struct dld_policy policy;
    struct bfd_storage storage;
    struct dld_binding binding;
    struct dld_table tables[2];
    struct dld_image image;
    uint32_t bound, mapped, faulted, table_commits;
    uint32_t mapped_pages;
};

/* One exact synthetic requester only: bus1/device0/function0 RID0x100.
 * SID0..15 is explicit fake input, not inferred hardware stream ownership.
 * policy aperture must be the frozen APCIE range; RAM/reservations caller
 * supplied and stable. Output must start zeroed and may not be reinitialized.
 * Storage, workspace, map and policy arrays must be stable and nonaliasing.
 * No borrowed firmware roots or table/data PA aliases are accepted. */
int bfd_init(struct bfd_map *, const struct apb_io *checked_fake,
              const struct dld_policy *, const struct bfd_storage *, uint32_t rid, uint32_t sid);

/* One bounded contiguous map,16K alignment, <=256 pages in a single32MiB L1
 * span. Staging validates before committing either fake output page. No
 * append/unmap/reuse, TTBR write, cache maintenance or invalidation API exists.
 * Link-down before entry leaves storage and workspace unchanged. */
int bfd_map_pages(struct bfd_map *, struct bfd_workspace *, uint64_t iova,
                   uint64_t pa, uint64_t bytes, unsigned permissions);

/* Fresh link/PCI and exact requester checks before every software lookup.
 * Output PA must not alias any input/workspace. It is unchanged on every
 * error; no fake output-table writes ever.
 * Workspace may change on an admitted lookup, but stays unchanged if link
 * down. This is not a device translation transaction or isolation proof. */
int bfd_translate(struct bfd_map *, struct bfd_workspace *, uint32_t rid, uint32_t sid,
                   uint64_t iova, unsigned access, uint64_t *pa);

/* Default/KERNEL all operations return BFD_GATED, no I/O/buffer changes.
 * BFD_FAKE_DEVICE is a host-only compile flag, refused with KERNEL. */
int bfd_fake_map_compiled(void);
#ifdef __cplusplus
}
#endif
#endif
