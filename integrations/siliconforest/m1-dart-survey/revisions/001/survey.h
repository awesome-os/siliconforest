/* Original darwin-lab read-only survey. License pending; see PROVENANCE.json. */
#ifndef DL_DART_SURVEY_H
#define DL_DART_SURVEY_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum dls_result { DLS_SAMPLED=0, DLS_NOT_READY=1, DLS_UNSUPPORTED=2,
    DLS_ARGUMENT=-1, DLS_BOARD=-2, DLS_IO=-3, DLS_LIMIT=-4 };
enum dls_region { DLS_PMGR, DLS_RC, DLS_PHY, DLS_PORT, DLS_DART, DLS_REGIONS };
#define DLS_SAMPLES 88u
#define DLS_MAX_READS 1024u
typedef int (*dls_property_fn)(void *, const char *, const char *, const uint8_t **, size_t *);
struct dls_board { uint64_t base[DLS_REGIONS]; uint32_t size[DLS_REGIONS]; };
struct dls_io { void *context; int (*read32)(void *, unsigned, uint32_t, uint32_t *); };
struct dls_sample { uint32_t offset, value; };
struct dls_snapshot {
    uint32_t version, sample_count, reads_attempted, failure_region, failure_offset;
    int32_t result;
    uint32_t power[4], clocks[5];
    struct dls_sample samples[DLS_SAMPLES];
};
/* Stable borrowed ADT properties; no property, address or callback may mutate
 * during validation/sampling. Caller owns buffer lifetime and serialization.
 * A board value is validation output, not a hardware-readiness capability.
 */
int dls_validate_board(dls_property_fn get, void *context, struct dls_board *out);
int dls_read_allowed(unsigned region, uint32_t offset);
/* Only read32 exists: no write, power, reset, retry, table walk or adoption.
 * Partial numeric samples survive refusal/error. No callback fault recovery is
 * provided: a synchronous MMIO abort/hang cannot be made safe by a read count.
 */
int dls_sample(const struct dls_board *, const struct dls_io *, struct dls_snapshot *);
#ifdef __cplusplus
}
#endif
#endif
