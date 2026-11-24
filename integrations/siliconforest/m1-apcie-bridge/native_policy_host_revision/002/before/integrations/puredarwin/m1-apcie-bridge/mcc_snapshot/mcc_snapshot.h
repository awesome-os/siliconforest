/* Original T8103 metadata decoder/read-plan offsets. No hardware backend. */
#ifndef DARWIN_LAB_MCC_SNAPSHOT_H
#define DARWIN_LAB_MCC_SNAPSHOT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define MCP_PROFILE_T8103 1u
#define MCP_PLANES 4u
#define MCP_SLOTS 4u
#define MCP_FIELDS 3u
#define MCP_PASSES 2u
#define MCP_READS (MCP_PLANES*MCP_SLOTS*MCP_FIELDS*MCP_PASSES)
#define MCP_BYTES (MCP_READS*4u)
#define MCP_DRAM_BASE UINT64_C(0x800000000)
#define MCP_PA_LIMIT UINT64_C(0x1000000000)
enum mcp_status { MCP_OK=0, MCP_ARGUMENT=-1, MCP_PROFILE=-2,
    MCP_RANGE=-3, MCP_CHANGED=-4, MCP_PLANES_DIFFER=-5, MCP_ENABLED_INVALID=-6 };
enum mcp_field { MCP_START=0, MCP_END=1, MCP_ENABLE=2 };
enum mcp_coverage { MCP_FOUR_PLANES_FOUR_TZ_ONLY=1 };
struct mcp_range { uint64_t base, bytes; };
struct mcp_read { uint64_t offset; uint32_t pass, plane, slot, field; };
struct mcp_slot {
    uint32_t start_word, end_word, enable_word, enabled;
    struct mcp_range range; /* exact4KiB source units, no CPU-page rounding */
};
struct mcp_result {
    uint32_t profile, coverage, passes, planes, slots, enabled_count, union_count;
    struct mcp_range dram;
    struct mcp_slot slot[MCP_SLOTS];
    struct mcp_range excluded[MCP_SLOTS]; /* exact overlap/adjacency union */
};
/* Index order: pass,plane,slot,(start,end,enable). Offsets are relative to a
 * separately validated MCC plane-region mapping. This neither obtains nor
 * authorizes that mapping, performs an access, or proves reads are safe. */
int mcp_plan_read(uint32_t profile, uint32_t index, uint64_t region_bytes,
                  struct mcp_read *out);
/* Two complete little-endian snapshots, all4planes. Accessible immutable
 * source/DRAM inputs and serialization remain caller obligations. Output must
 * not overlap inputs. Every failure preserves output. No allocator/callback,
 * pointer-to-physical conversion, MMIO or policy/complete flag exists.
 * Success covers only the declared source profile, never global exclusions. */
int mcp_decode(uint32_t profile, const uint8_t *raw, size_t bytes,
               const struct mcp_range *dram, struct mcp_result *out);
#ifdef __cplusplus
}
#endif
#endif
