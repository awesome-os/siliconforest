/* Original detached source-profile codec. License selection pending. */
#ifndef AGXC_CHANNEL_H
#define AGXC_CHANNEL_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define AGXC_RANGE_MAX 16u
#define AGXC_SLOTS 256u
enum agxc_result { AGXC_OK, AGXC_ARGUMENT, AGXC_ABI_UNKNOWN, AGXC_PROFILE,
    AGXC_SIZE, AGXC_OPCODE, AGXC_PATTERN, AGXC_RANGE, AGXC_INDEX, AGXC_EMPTY, AGXC_FULL };
enum agxc_firmware { AGXC_FW_UNKNOWN, AGXC_V13_5 };
enum agxc_gpu { AGXC_GPU_UNKNOWN, AGXC_G13 };
enum agxc_evidence { AGXC_BOOT_METADATA, AGXC_PINNED_SOURCE_TEST, AGXC_LOADED_IMAGE };
enum agxc_profile { AGXC_NO_PROFILE, AGXC_V13_5_G13_SOURCE_ONLY };
enum agxc_channel { AGXC_DEVCTRL, AGXC_FWCTL };
enum agxc_dc_pattern { AGXC_DC_INIT = 0x1a, AGXC_DC_UPDATE_IDLE_TS = 0x23 };
/* Numeric facts only: no owner, CPU pointer, physical mapping or hardware grant. */
struct agxc_synthetic_range { uint64_t address, bytes; };
struct agxc_range_facts { const struct agxc_synthetic_range *ranges; size_t count; };
struct agxc_fwctl_pattern { uint64_t address; uint32_t context_id; };
struct agxc_ring_snapshot { uint32_t read_index, write_index; };
struct agxc_ring_candidate { uint32_t index, next_index, occupied, available; size_t byte_offset; };
/* Only explicit pinned SOURCE testing can select a profile. Boot metadata and
 * loaded-image requests return UNKNOWN; this leaf has no loaded-image evidence. */
enum agxc_result agxc_select(enum agxc_firmware, enum agxc_gpu, enum agxc_evidence, enum agxc_profile *);
/* Exact sizes required. Failure never modifies output. Disjoint input/output
 * buffers required. DC data bytes are zero-default policy, not known reserved. */
enum agxc_result agxc_dc_encode(enum agxc_profile, enum agxc_dc_pattern, uint8_t *, size_t);
enum agxc_result agxc_dc_decode(enum agxc_profile, const uint8_t *, size_t, enum agxc_dc_pattern *);
/* Numeric FWCtl source pattern only: address0 is accepted as the source default;
 * nonzero address must be aligned/canonical and covered for 16KiB by facts.
 * Context is uninterpreted u32. Neither range nor pattern authorizes invalidate. */
enum agxc_result agxc_fwctl_encode(enum agxc_profile, const struct agxc_fwctl_pattern *,
    struct agxc_range_facts, uint8_t *, size_t);
enum agxc_result agxc_fwctl_decode(enum agxc_profile, const uint8_t *, size_t,
    struct agxc_range_facts, struct agxc_fwctl_pattern *);
/* Decode only the documented index fields; unknown state bytes are opaque.
 * No write/commit/doorbell API. Candidates do not advance a real ring. */
enum agxc_result agxc_state_decode(enum agxc_profile, enum agxc_channel,
    const uint8_t *, size_t, struct agxc_ring_snapshot *);
/* One-empty-slot FULL is conservative local policy, not firmware evidence. */
enum agxc_result agxc_ring_candidate(enum agxc_profile, enum agxc_channel,
    struct agxc_ring_snapshot, int transmit, struct agxc_ring_candidate *);
#ifdef __cplusplus
}
#endif
#endif
