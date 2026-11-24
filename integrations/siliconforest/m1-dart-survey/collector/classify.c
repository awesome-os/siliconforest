#include "classify.h"
#include "observation_layout.h"
#include "survey.h"
#include <string.h>

_Static_assert(DLS_SNAPSHOT_BYTES == sizeof(struct dls_snapshot), "v1 wire size");
_Static_assert(DLS_SAMPLE_CAP == DLS_SAMPLES, "sample cap");
_Static_assert(DLS_READ_CAP == DLS_MAX_READS, "read cap");
_Static_assert(DLS_REGION_CAP == DLS_REGIONS, "region sentinel");
_Static_assert(DLS_SAMPLED == 0 && DLS_NOT_READY == 1 && DLS_UNSUPPORTED == 2, "status");
_Static_assert(DLS_ARGUMENT == -1 && DLS_BOARD == -2 && DLS_IO == -3 && DLS_LIMIT == -4, "status");

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int dls_nonce_ok(const char *text)
{
    unsigned i;
    if (!text) return 0;
    for (i = 0; i < DLS_NONCE_HEX; i++) {
        char c = text[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0;
    }
    return text[DLS_NONCE_HEX] == '\0';
}

int dls_take_nonce(int argc, char **argv, const char **nonce)
{
    if (!nonce) return DLS_EXIT_USAGE;
    *nonce = 0;
    if (argc != 3 || !argv || !argv[1] || !argv[2]) return DLS_EXIT_USAGE;
    if (strcmp(argv[1], "--nonce") != 0) return DLS_EXIT_USAGE;
    if (!dls_nonce_ok(argv[2])) return DLS_EXIT_USAGE;
    *nonce = argv[2];
    return 0;
}

int dls_prefix_offset(unsigned index, uint32_t *offset)
{
    /* survey.c sample_one order: four probes, four 0x80 words, then 16 groups. */
    static const uint32_t head[8] = {0u, 4u, 0x60u, 0xfcu, 0x80u, 0x84u, 0x88u, 0x8cu};
    unsigned sid, sub;
    if (!offset || index >= DLS_SAMPLE_CAP) return -1;
    if (index < 8u) {
        *offset = head[index];
        return 0;
    }
    sid = (index - 8u) / 5u;
    sub = (index - 8u) % 5u;
    if (sid >= 16u) return -1;
    if (sub == 0u) *offset = 0x100u + 4u * sid;
    else *offset = 0x200u + 16u * sid + 4u * (sub - 1u);
    return 0;
}

int dls_decode(const uint8_t *bytes, size_t length, struct dls_decoded *out)
{
    uint32_t i;
    if (!bytes || !out || length != DLS_SNAPSHOT_BYTES) return -1;
    memset(out, 0, sizeof(*out));
    out->version = le32(bytes + 0);
    out->sample_count = le32(bytes + 4);
    out->reads_attempted = le32(bytes + 8);
    out->failure_region = le32(bytes + 12);
    out->failure_offset = le32(bytes + 16);
    out->result = (int32_t)le32(bytes + 20);
    for (i = 0; i < 4u; i++) out->power[i] = le32(bytes + 24 + 4u * i);
    for (i = 0; i < 5u; i++) out->clocks[i] = le32(bytes + 40 + 4u * i);
    for (i = 0; i < DLS_SAMPLE_CAP; i++) {
        out->sample_offset[i] = le32(bytes + 60 + 8u * i);
        out->sample_value[i] = le32(bytes + 64 + 8u * i);
    }
    return 0;
}

static int known_result(int32_t result)
{
    return result == 0 || result == 1 || result == 2 || result == -1 || result == -2 ||
        result == -3 || result == -4;
}

int dls_classify(const struct dls_decoded *decoded, int *reason)
{
    uint32_t i;
    if (reason) *reason = DLS_REASON_NONE;
    if (!decoded) {
        if (reason) *reason = DLS_REASON_LENGTH;
        return DLS_EXIT_FAIL;
    }
    if (decoded->version != 1u) {
        if (reason) *reason = DLS_REASON_VERSION;
        return DLS_EXIT_FAIL;
    }
    if (decoded->sample_count > DLS_SAMPLE_CAP) {
        if (reason) *reason = DLS_REASON_COUNT;
        return DLS_EXIT_FAIL;
    }
    if (decoded->reads_attempted > DLS_READ_CAP) {
        if (reason) *reason = DLS_REASON_READS;
        return DLS_EXIT_FAIL;
    }
    if (decoded->failure_region > DLS_REGION_CAP) {
        if (reason) *reason = DLS_REASON_REGION;
        return DLS_EXIT_FAIL;
    }
    if (!known_result(decoded->result)) {
        if (reason) *reason = DLS_REASON_RESULT;
        return DLS_EXIT_FAIL;
    }
    for (i = 0; i < decoded->sample_count; i++) {
        uint32_t expect = 0;
        if (dls_prefix_offset(i, &expect) != 0 || decoded->sample_offset[i] != expect) {
            if (reason) *reason = DLS_REASON_PREFIX;
            return DLS_EXIT_FAIL;
        }
    }
    if (decoded->result == 0) {
        if (decoded->sample_count != DLS_SAMPLED_COUNT ||
            decoded->reads_attempted != DLS_SAMPLED_READS ||
            decoded->failure_region != DLS_REGION_CAP || decoded->failure_offset != 0u) {
            if (reason) *reason = DLS_REASON_SAMPLED;
            return DLS_EXIT_FAIL;
        }
        return DLS_EXIT_SAMPLED;
    }
    return DLS_EXIT_REFUSAL;
}

const char *dls_reason_name(int reason)
{
    switch (reason) {
    case DLS_REASON_LENGTH: return "length";
    case DLS_REASON_VERSION: return "version";
    case DLS_REASON_COUNT: return "sample_count";
    case DLS_REASON_READS: return "reads";
    case DLS_REASON_REGION: return "region";
    case DLS_REASON_RESULT: return "result";
    case DLS_REASON_PREFIX: return "prefix";
    case DLS_REASON_SAMPLED: return "sampled";
    default: return "none";
    }
}
