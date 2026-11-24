/* Original GPU-specific metadata/PMGR observation. No GPU register access. */
#ifndef DL_GPU_PREFLIGHT_H
#define DL_GPU_PREFLIGHT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define GPF_BYTES 64u
enum gpf_result { GPF_OBSERVED=0, GPF_BOARD_REFUSED=1, GPF_READ_FAILED=2 };
typedef int (*gpf_property_fn)(void *, const char *, const char *, const uint8_t **, size_t *);
struct gpf_board { uint64_t pmgr_base; uint32_t pmgr_size, gfx_offset; };
struct gpf_snapshot {
    uint32_t version, byte_size, result, board_valid;
    uint32_t reads_attempted, read_completed, gfx_power, reserved;
    uint32_t clock_ids[3], virtual_mask, physical_domain, pmgr_offset;
    uint32_t gpu_mmio_reads, gpu_mmio_authorized;
};
/* Borrowed stable property bytes; bounded owned lifetime during validation.
 * Success validates metadata only; does not qualify GPU register access. */
int gpf_validate_board(gpf_property_fn, void *, struct gpf_board *);
/* A single optional read of PMGR GFX99 at physical0x23b7003f8. No callback
 * exists for writes, GPU/ASC/UAT, firmware, tables, PCIe or framebuffer. */
int gpf_observe(const struct gpf_board *, int (*read32)(void *, uint32_t, uint32_t *),
                void *, struct gpf_snapshot *);
#ifdef __cplusplus
}
#endif
#if defined(__cplusplus)
static_assert(sizeof(gpf_snapshot)==GPF_BYTES,"snapshot ABI");
static_assert(offsetof(gpf_snapshot,gpu_mmio_reads)==56,"GPU read count ABI");
#else
_Static_assert(sizeof(struct gpf_snapshot)==GPF_BYTES,"snapshot ABI");
_Static_assert(offsetof(struct gpf_snapshot,gpu_mmio_reads)==56,"GPU read count ABI");
#endif
#endif
