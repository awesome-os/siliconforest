/* Trusted internal/failure-test seam; not an alternate registry or security API. */
#ifndef DARWIN_LAB_NATIVE_POLICY_READER_INTERNAL_H
#define DARWIN_LAB_NATIVE_POLICY_READER_INTERNAL_H
#include "policy_reader.h"
#ifdef __cplusplus
extern "C" {
#endif
enum npr_source_id { NPR_DRAM_BASE=0,NPR_DRAM_SIZE=1,NPR_FACTS_DATA=2 };
struct npr_view { const uint8_t *bytes; size_t count; uintptr_t object; };
struct npr_boot_view { const uint8_t *bytes; size_t count; uintptr_t object; uint32_t initialized; };
struct npr_ops {
    void *context;
    int (*begin)(void *); /* Called once. finish also runs after begin fails. */
    int (*boot)(void *,struct npr_boot_view *);
    int (*view)(void *,enum npr_source_id,struct npr_view *);
    int (*finish)(void *); /* Release every partial acquisition, no retry. */
};
/* Callbacks/context are trusted code, not caller-selectable hardware authority.
 * All four struct spans disjoint; borrowed spans disjoint from them/each other.
 * Inputs remain immutable under external serialization. No exported raw view. */
int npr_read_with_ops(const struct npr_ops *,const struct nps_expected *,
                      struct npr_workspace *,struct nps_result *);
#ifdef __cplusplus
}
#endif
#endif
