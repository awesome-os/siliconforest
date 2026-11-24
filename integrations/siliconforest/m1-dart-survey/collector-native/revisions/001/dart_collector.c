/* Original read-only collector for PDDARTSurveyDiagnosticV1. License pending.
 * Pure C: validates and prints caller-supplied bytes. No IOKit, MMIO or retry.
 */
#include "dart_collector.h"
#include <string.h>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/sha.h>
#endif

enum { REGION_PMGR, REGION_RC, REGION_PHY, REGION_PORT, REGION_DART, REGION_NONE };
enum { SAMPLES = 88, FULL_READS = 889, GUARD_READS = 9 };
static const char PROVIDER_KEY[] = "PDDARTSurveyDiagnosticV1";
static const char INSTANCE_KEY[] = "PDDARTSurveyObservationV1";

/* The reviewed sampler's exact order (m1-dart-survey survey.c, unchanged in rev002). */
static const uint32_t guard_region[GUARD_READS] = {
    REGION_PMGR, REGION_PMGR, REGION_PMGR, REGION_PMGR, REGION_PHY, REGION_RC,
    REGION_PORT, REGION_PORT, REGION_PORT};
static const uint32_t guard_offset[GUARD_READS] = {
    0x1b8, 0x348, 0x3e8, 0x1a0, 0x0, 0x24, 0x800, 0x814, 0x804};

static uint32_t expected_offset(unsigned i)
{
    static const uint32_t head[8] = {0x00, 0x04, 0x60, 0xfc, 0x80, 0x84, 0x88, 0x8c};
    if (i < 8) return head[i];
    i -= 8;
    unsigned sid = i / 5, slot = i % 5;
    return slot == 0 ? 0x100 + 4 * sid : 0x200 + 16 * sid + 4 * (slot - 1);
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int dlc_nonce_valid(const char *n)
{
    if (!n || strlen(n) != 48) return 0;
    for (size_t i = 0; i < 48; i++)
        if (!((n[i] >= '0' && n[i] <= '9') || (n[i] >= 'a' && n[i] <= 'f'))) return 0;
    return 1;
}

static int allowed_region_offset(uint32_t r, uint32_t o)
{
    for (unsigned i = 0; i < GUARD_READS; i++)
        if (r == guard_region[i] && o == guard_offset[i]) return 1;
    if (r != REGION_DART) return 0;
    for (unsigned i = 0; i < SAMPLES; i++) if (o == expected_offset(i)) return 1;
    return 0;
}

int dlc_validate(const uint8_t *b, size_t n, struct dlc_summary *out)
{
    if (!b || !out || n != DLC_DIAG_BYTES || le32(b) != 1) return -1;
    const uint32_t s = le32(b + 4), reads = le32(b + 8), region = le32(b + 12), off = le32(b + 16);
    const int32_t result = (int32_t)le32(b + 20);
    const uint8_t *samples = b + 60;
    if (result < -4 || result > 2) return -1;                      /* known dls_result only */
    if (s > SAMPLES || reads < 10 * s || reads > 10 * s + 10 || reads > FULL_READS) return -1;
    for (unsigned i = 0; i < SAMPLES; i++) {                       /* successful prefix, zero tail */
        uint32_t so = le32(samples + 8 * i), sv = le32(samples + 8 * i + 4);
        if (i < s ? so != expected_offset(i) : (so || sv)) return -1;
    }
    if (region > REGION_NONE || (region == REGION_NONE ? off != 0 : !allowed_region_offset(region, off)))
        return -1;
    const uint32_t step = reads - 10 * s;                          /* reads in the stopping step */
    const int guard_at = step >= 1 && step <= GUARD_READS &&
        region == guard_region[step - 1] && off == guard_offset[step - 1];
    switch (result) {
    case 0:                                                        /* DLS_SAMPLED */
        if (s != SAMPLES || reads != FULL_READS || region != REGION_NONE ||
            ((le32(samples + 4) >> 24) & 15) != 14 || (le32(samples + 16 + 4) & 0x8000u) ||
            (le32(samples + 24 + 4) & 0xffff0000u)) return -1;
        break;
    case 1:                                                        /* DLS_NOT_READY: a guard read */
        if (!guard_at) return -1;
        break;
    case 2: {                                                      /* DLS_UNSUPPORTED */
        uint32_t v = s ? le32(samples + 8 * (s - 1) + 4) : 0;
        int cause = (s == 1 && ((v >> 24) & 15) != 14) || (s == 3 && (v & 0x8000u)) ||
                    (s == 4 && (v & 0xffff0000u));
        if (!cause || step != 0 || region != REGION_DART || off != expected_offset(s - 1)) return -1;
        break;
    }
    case -3:                                                       /* DLS_IO */
        if (!(reads == 0 && region == REGION_NONE) && !guard_at &&
            !(step == 10 && s < SAMPLES && region == REGION_DART && off == expected_offset(s)))
            return -1;
        break;
    case -1: case -2:                                              /* argument/board refusal */
        if (reads || region != REGION_NONE) return -1;
        break;
    default:                                                       /* DLS_LIMIT: bounds only */
        break;
    }
    out->sample_count = s; out->reads = reads; out->region = region; out->offset = off; out->result = result;
    return 0;
}

static void emit_raw(FILE *f, const char *nonce, const char *key, const uint8_t *b, size_t n)
{
    static const char hex[] = "0123456789abcdef";
    unsigned char digest[32];
    char line[129], digest_hex[65];
#ifdef __APPLE__
    CC_SHA256(b, (CC_LONG)n, digest);
#else
    SHA256(b, n, digest);
#endif
    for (unsigned i = 0; i < 32; i++) { digest_hex[2 * i] = hex[digest[i] >> 4]; digest_hex[2 * i + 1] = hex[digest[i] & 15]; }
    digest_hex[64] = 0;
    fprintf(f, "DL13_DART_RECORD_BEGIN %s %s bytes=%zu chunk_bytes=64\n", nonce, key, n);
    size_t chunks = 0;
    for (size_t at = 0; at < n; at += 64) {
        size_t c = n - at < 64 ? n - at : 64;
        for (size_t i = 0; i < c; i++) { line[2 * i] = hex[b[at + i] >> 4]; line[2 * i + 1] = hex[b[at + i] & 15]; }
        line[2 * c] = 0;
        fprintf(f, "DL13_DART_RECORD_HEX %s %s index=%zu %s\n", nonce, key, chunks++, line);
    }
    fprintf(f, "DL13_DART_RECORD_END %s %s bytes=%zu chunks=%zu sha256=%s\n", nonce, key, n, chunks, digest_hex);
}

static const char *get_status(int g)
{
    return g == DLC_GET_NO_ENTRY ? "no_entry" : g == DLC_GET_NO_PROPERTY ? "no_property" : "error";
}

int dlc_run(int argc, char **argv, dlc_getter get, void *ctx, FILE *out)
{
    static uint8_t provider[DLC_MAX_RAW], instance[DLC_MAX_RAW];
    size_t plen = 0, ilen = 0;
    int32_t pdetail = 0, idetail = 0;
    struct dlc_summary sum;
    if (argc != 3 || strcmp(argv[1], "--nonce") || !dlc_nonce_valid(argv[2]) || !get || !out) {
        fputs("usage: darwin-dart-survey --nonce <48 lowercase hex>\n", stderr);
        return DLC_EXIT_USAGE;
    }
    const char *nonce = argv[2];
    int pg = get(ctx, DLC_PROVIDER, provider, sizeof(provider), &plen, &pdetail);
    if (pg == DLC_GET_OK && plen > sizeof(provider)) pg = DLC_GET_ERROR;
    if (pg == DLC_GET_OK) emit_raw(out, nonce, PROVIDER_KEY, provider, plen);
    else fprintf(out, "DL13_DART_COPY %s %s status=%s detail=%d\n", nonce, PROVIDER_KEY, get_status(pg), (int)pdetail);
    const char *verdict, *inst = "not_read";
    int code = DLC_EXIT_FAIL, wellformed = pg == DLC_GET_OK && dlc_validate(provider, plen, &sum) == 0;
    if (pg != DLC_GET_OK) verdict = pg == DLC_GET_ERROR ? "MIG_ERROR" : "MISSING";
    else if (!wellformed) verdict = "MALFORMED";
    else {
        int ig = get(ctx, DLC_INSTANCE, instance, sizeof(instance), &ilen, &idetail);
        if (ig == DLC_GET_OK && ilen > sizeof(instance)) ig = DLC_GET_ERROR;
        if (ig == DLC_GET_OK) emit_raw(out, nonce, INSTANCE_KEY, instance, ilen);
        else if (ig != DLC_GET_NO_ENTRY)
            fprintf(out, "DL13_DART_COPY %s %s status=%s detail=%d\n", nonce, INSTANCE_KEY, get_status(ig), (int)idetail);
        if (ig == DLC_GET_NO_ENTRY) inst = "absent";
        else if (ig == DLC_GET_OK && ilen == plen && !memcmp(instance, provider, plen)) inst = "equal";
        else inst = ig == DLC_GET_OK ? "mismatch" : get_status(ig);
        if (strcmp(inst, "absent") && strcmp(inst, "equal")) verdict = "INSTANCE_MISMATCH";
        else if (sum.result == 0) { verdict = "SAMPLED"; code = DLC_EXIT_SAMPLED; }
        else { verdict = "REFUSED"; code = DLC_EXIT_REFUSED; }
    }
    if (wellformed)
        fprintf(out, "DL13_DART_VERDICT %s verdict=%s result=%d samples=%u reads=%u region=%u offset=%u instance=%s initialization_proven=false\n",
                nonce, verdict, (int)sum.result, sum.sample_count, sum.reads, sum.region, sum.offset, inst);
    else
        fprintf(out, "DL13_DART_VERDICT %s verdict=%s instance=%s initialization_proven=false\n", nonce, verdict, inst);
    if (fflush(out) || ferror(out)) return DLC_EXIT_FAIL;
    return code;
}
