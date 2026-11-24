/* Original read-only diagnostic parser. License pending; see PROVENANCE.json. */
#ifndef DLS_COLLECT_CLASSIFY_H
#define DLS_COLLECT_CLASSIFY_H
#include <stddef.h>
#include <stdint.h>

#define DLS_NONCE_HEX 48u
#define DLS_SNAPSHOT_BYTES 764u
#define DLS_HEX_CHUNK 16u
#define DLS_SAMPLE_CAP 88u
#define DLS_READ_CAP 1024u
#define DLS_REGION_CAP 5u
#define DLS_SAMPLED_COUNT 88u
#define DLS_SAMPLED_READS 889u

enum {
    DLS_EXIT_SAMPLED = 0,
    DLS_EXIT_FAIL = 1,
    DLS_EXIT_USAGE = 2,
    DLS_EXIT_REFUSAL = 77
};

enum {
    DLS_REASON_NONE = 0,
    DLS_REASON_LENGTH,
    DLS_REASON_VERSION,
    DLS_REASON_COUNT,
    DLS_REASON_READS,
    DLS_REASON_REGION,
    DLS_REASON_RESULT,
    DLS_REASON_PREFIX,
    DLS_REASON_SAMPLED
};

struct dls_decoded {
    uint32_t version, sample_count, reads_attempted, failure_region, failure_offset;
    int32_t result;
    uint32_t power[4];
    uint32_t clocks[5];
    uint32_t sample_offset[DLS_SAMPLE_CAP];
    uint32_t sample_value[DLS_SAMPLE_CAP];
};

int dls_nonce_ok(const char *text);
int dls_take_nonce(int argc, char **argv, const char **nonce);
int dls_prefix_offset(unsigned index, uint32_t *offset);
int dls_decode(const uint8_t *bytes, size_t length, struct dls_decoded *out);
int dls_classify(const struct dls_decoded *decoded, int *reason);
const char *dls_reason_name(int reason);
#endif
