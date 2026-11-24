/* Host-only adversarial tests: real reviewed sampler snapshots + fake registry. */
#ifdef NDEBUG
#error DLC_TEST_REQUIRES_ASSERTIONS_NDEBUG_FORBIDDEN
#endif
#define _GNU_SOURCE
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "dart_collector.h"
#include "../survey.h"
#include "../observation_layout.h"

static const char NONCE[] = "0123456789abcdef0123456789abcdef0123456789abcdef";

/* ---- fake registry ------------------------------------------------------- */
struct fake {
    int pg, ig; int32_t pdetail, idetail;
    const uint8_t *p; size_t pn; const uint8_t *i; size_t in;
    int pcalls, icalls;
};
static int fake_get(void *ctx, enum dlc_copy which, uint8_t *buf, size_t cap, size_t *len, int32_t *detail)
{
    struct fake *f = ctx;
    int g = which == DLC_PROVIDER ? f->pg : f->ig;
    const uint8_t *src = which == DLC_PROVIDER ? f->p : f->i;
    size_t n = which == DLC_PROVIDER ? f->pn : f->in;
    if (which == DLC_PROVIDER) f->pcalls++; else f->icalls++;
    *detail = which == DLC_PROVIDER ? f->pdetail : f->idetail;
    if (g != DLC_GET_OK) return g;
    assert(n <= cap);
    memcpy(buf, src, n); *len = n;
    return DLC_GET_OK;
}
static char *last_output;
static int run_with(struct fake *f, int argc, char **argv)
{
    size_t size = 0; char *text = NULL;
    FILE *out = open_memstream(&text, &size);
    assert(out);
    int code = dlc_run(argc, argv, fake_get, f, out);
    fclose(out);
    free(last_output); last_output = text;
    return code;
}
static int run(struct fake *f)
{
    char *argv[] = {(char *)"darwin-dart-survey", (char *)"--nonce", (char *)NONCE, NULL};
    return run_with(f, 3, argv);
}
static int contains(const char *needle) { return last_output && strstr(last_output, needle) != NULL; }
static unsigned count_lines(void)
{
    unsigned n = 0;
    for (const char *p = last_output; p && *p; p++) n += *p == '\n';
    return n;
}

/* ---- real sampler with a fake register bank -------------------------------- */
enum fault { NONE, IO_AT, NOT_READY_AT, PAGE_SHIFT, LOCKED, HIGH_STREAMS };
struct bank { enum fault kind; unsigned at, calls; };
static int bank_read(void *ctx, unsigned region, uint32_t off, uint32_t *out)
{
    struct bank *b = ctx;
    unsigned n = ++b->calls;
    assert(dls_read_allowed(region, off));
    if (b->kind == IO_AT && n == b->at) return -1;
    uint32_t v;
    switch (region) {
    case DLS_PMGR: v = 0xff; break;
    case DLS_PHY: v = 0x0f; break;
    case DLS_RC: case DLS_PORT: v = 1; break;
    default:
        v = off == 0 ? 0x0e000000u : off == 0x60 ? 0 : off == 0xfc ? 0x0000ffffu : 0x80000000u | off;
        if (b->kind == PAGE_SHIFT && off == 0) v = 0x0c000000u;
        if (b->kind == LOCKED && off == 0x60) v = 0x8000u;
        if (b->kind == HIGH_STREAMS && off == 0xfc) v = 0x00010000u;
    }
    if (b->kind == NOT_READY_AT && n == b->at && region != DLS_DART)  /* ACTUAL!=TARGET, PHY reset, RUN clear */
        v = region == DLS_PMGR ? 0x0fu : region == DLS_PHY ? 0x8fu : 0;
    *out = v;
    return 0;
}
static const struct dls_board BOARD = {
    {UINT64_C(0x23b700000), UINT64_C(0x680000000), UINT64_C(0x680080000), UINT64_C(0x681000000), UINT64_C(0x681008000)},
    {0x4000, 0x4000, 0x4000, 0x4000, 0x4000}};
static int sample(enum fault kind, unsigned at, uint8_t bytes[DLC_DIAG_BYTES])
{
    struct bank b = {kind, at, 0};
    struct dls_io io = {&b, bank_read};
    struct dls_snapshot s;
    int r = dls_sample(&BOARD, &io, &s);
    assert(r == s.result);
    memcpy(bytes, &s, sizeof(s));
    return r;
}
static void put32(uint8_t *b, size_t at, uint32_t v)
{
    b[at] = (uint8_t)v; b[at + 1] = (uint8_t)(v >> 8); b[at + 2] = (uint8_t)(v >> 16); b[at + 3] = (uint8_t)(v >> 24);
}

static void expect_code(const uint8_t *bytes, size_t n, int code)
{
    struct fake f = {DLC_GET_OK, DLC_GET_NO_ENTRY, 0, 0, bytes, n, NULL, 0, 0, 0};
    int got = run(&f);
    if (got != code) { fprintf(stderr, "expected %d got %d\n%s", code, got, last_output); abort(); }
    assert(f.pcalls == 1 && f.icalls <= 1);
    assert(contains("DL13_DART_RECORD_BEGIN 0123456789abcdef0123456789abcdef0123456789abcdef PDDARTSurveyDiagnosticV1"));
    assert(contains("initialization_proven=false"));
}

int main(int argc, char **argv)
{
    uint8_t ok[DLC_DIAG_BYTES], tmp[DLC_DIAG_BYTES];
    unsigned groups = 0, snapshots = 0;
    struct dlc_summary sum;

    /* 1. Usage: exact CLI, getter never called. */
    {
        struct fake f = {DLC_GET_OK, DLC_GET_NO_ENTRY, 0, 0, ok, sizeof(ok), NULL, 0, 0, 0};
        char *a1[] = {(char *)"x", NULL};
        char *a2[] = {(char *)"x", (char *)"--nonce", NULL};
        char *a3[] = {(char *)"x", (char *)"--nonce", (char *)NONCE, (char *)"extra", NULL};
        char *a4[] = {(char *)"x", (char *)"-n", (char *)NONCE, NULL};
        assert(run_with(&f, 1, a1) == 2 && run_with(&f, 2, a2) == 2 && run_with(&f, 4, a3) == 2 && run_with(&f, 3, a4) == 2);
        const char *bad[] = {"0123456789ABCDEF0123456789abcdef0123456789abcdef", "0123456789abcdef0123456789abcdef0123456789abcde",
                             "0123456789abcdef0123456789abcdef0123456789abcdef0", "g123456789abcdef0123456789abcdef0123456789abcdef", ""};
        for (unsigned i = 0; i < 5; i++) {
            char *a[] = {(char *)"x", (char *)"--nonce", (char *)bad[i], NULL};
            assert(run_with(&f, 3, a) == 2 && count_lines() == 0);
        }
        assert(f.pcalls == 0 && f.icalls == 0);
        groups++;
    }

    /* 2. Real sampler success and every refusal position. */
    assert(sample(NONE, 0, ok) == DLS_SAMPLED);
    expect_code(ok, sizeof(ok), 0); snapshots++;
    assert(contains("verdict=SAMPLED result=0 samples=88 reads=889 region=5 offset=0 instance=absent"));
    assert(dlc_validate(ok, sizeof(ok), &sum) == 0 && sum.reads == 889);
    for (unsigned at = 1; at <= 889; at++) {
        assert(sample(IO_AT, at, tmp) == DLS_IO); expect_code(tmp, sizeof(tmp), 77); snapshots++;
        assert(contains("verdict=REFUSED result=-3"));
    }
    for (unsigned at = 1; at <= 889; at++) {
        int r = sample(NOT_READY_AT, at, tmp);
        if (r == DLS_SAMPLED) continue;                      /* DART read positions ignore guard faults */
        assert(r == DLS_NOT_READY); expect_code(tmp, sizeof(tmp), 77); snapshots++;
        assert(contains("verdict=REFUSED result=1"));
    }
    enum fault unsupported[3] = {PAGE_SHIFT, LOCKED, HIGH_STREAMS};
    for (unsigned i = 0; i < 3; i++) {
        assert(sample(unsupported[i], 0, tmp) == DLS_UNSUPPORTED); expect_code(tmp, sizeof(tmp), 77); snapshots++;
        assert(contains("verdict=REFUSED result=2"));
    }
    /* Adapter-level refusals: board mismatch from dls_sample, and the adapter's
     * pre-sampling records (version1, result, failure_region5, zero otherwise). */
    {
        struct dls_board wrong = BOARD; wrong.base[DLS_DART] += 0x4000;
        struct bank b = {NONE, 0, 0}; struct dls_io io = {&b, bank_read}; struct dls_snapshot s;
        assert(dls_sample(&wrong, &io, &s) == DLS_BOARD && b.calls == 0);
        memcpy(tmp, &s, sizeof(s)); expect_code(tmp, sizeof(tmp), 77); snapshots++;
        int32_t adapter[3] = {DLS_BOARD, DLS_IO, DLS_ARGUMENT};
        for (unsigned i = 0; i < 3; i++) {
            memset(&s, 0, sizeof(s)); s.version = 1; s.result = adapter[i]; s.failure_region = DLS_REGIONS;
            memcpy(tmp, &s, sizeof(s)); expect_code(tmp, sizeof(tmp), 77); snapshots++;
        }
    }
    groups++;

    /* 3. Malformed provider bytes: printed raw, verdict MALFORMED, exit 1. */
    {
        uint8_t big[DLC_MAX_RAW]; memset(big, 0, sizeof(big)); memcpy(big, ok, sizeof(ok));
        size_t lengths[] = {0, 1, 763, 765, DLC_MAX_RAW};
        for (unsigned i = 0; i < 5; i++) { expect_code(big, lengths[i], 1); assert(contains("verdict=MALFORMED")); }
        struct { size_t at; uint32_t v; } edits[] = {
            {0, 2}, {0, 0}, {20, 3}, {20, (uint32_t)-5}, {4, 89}, {8, 1025}, {8, 888}, {8, 890}, {12, 6},
            {16, 4}, {12, 4}, {60, 4}, {60 + 8 * 87, 0}, {20, 1}, {20, 2}, {20, (uint32_t)-1}, {20, (uint32_t)-2}};
        for (unsigned i = 0; i < sizeof(edits) / sizeof(edits[0]); i++) {
            memcpy(tmp, ok, sizeof(ok)); put32(tmp, edits[i].at, edits[i].v);
            expect_code(tmp, sizeof(tmp), 1);
        }
        memcpy(tmp, ok, sizeof(ok)); put32(tmp, 64, 0x0c000000u); expect_code(tmp, sizeof(tmp), 1);  /* SAMPLED w/o shift14 */
        memcpy(tmp, ok, sizeof(ok)); put32(tmp, 60 + 16 + 4, 0x8000u); expect_code(tmp, sizeof(tmp), 1); /* SAMPLED but locked */
        assert(sample(IO_AT, 15, tmp) == DLS_IO);                    /* refusal with nonzero tail */
        put32(tmp, 60 + 8 * 80 + 4, 1); expect_code(tmp, sizeof(tmp), 1);
        assert(sample(IO_AT, 15, tmp) == DLS_IO); put32(tmp, 16, 0x1a0); expect_code(tmp, sizeof(tmp), 1); /* wrong position */
        assert(sample(NOT_READY_AT, 3, tmp) == DLS_NOT_READY); put32(tmp, 20, (uint32_t)DLS_UNSUPPORTED); expect_code(tmp, sizeof(tmp), 1);
        /* C1: SAMPLED requires every final guard word to pass (4 power + 5 clock). */
        static const uint32_t failing[9] = {0, 0x100000ffu, 0x10ffu, 0x4ffu, 0x8fu, 0, 2, 0, 0x10u};  /* PMGR x4, PHY reset, RUN/EN clear x4 */
        for (unsigned i = 0; i < 9; i++) {
            memcpy(tmp, ok, sizeof(ok)); put32(tmp, i < 4 ? 24 + 4 * i : 40 + 4 * (i - 4), failing[i]);
            expect_code(tmp, sizeof(tmp), 1); assert(contains("verdict=MALFORMED"));
        }
        /* R1: NOT_READY must carry a freshly read failing guard word. */
        assert(sample(NOT_READY_AT, 1, tmp) == DLS_NOT_READY); put32(tmp, 24, 0xffu); expect_code(tmp, sizeof(tmp), 1);
        assert(sample(NOT_READY_AT, 15, tmp) == DLS_NOT_READY); put32(tmp, 40, 0x0fu); expect_code(tmp, sizeof(tmp), 1);
        assert(sample(NOT_READY_AT, 3, tmp) == DLS_NOT_READY); put32(tmp, 16, 0x1b8); expect_code(tmp, sizeof(tmp), 1);
        {   /* Astra's reproduction: one passing PMGR read labelled NOT_READY. */
            struct dls_snapshot z; memset(&z, 0, sizeof(z));
            z.version = 1; z.reads_attempted = 1; z.failure_region = DLS_PMGR; z.failure_offset = 0x1b8;
            z.result = DLS_NOT_READY; z.power[0] = 0xff; memcpy(tmp, &z, sizeof(z)); expect_code(tmp, sizeof(tmp), 1);
            z.power[0] = 0x0f; memcpy(tmp, &z, sizeof(z)); expect_code(tmp, sizeof(tmp), 77);
            /* R1: DLS_LIMIT is unreachable under the frozen 889-read profile. */
            memset(&z, 0, sizeof(z)); z.version = 1; z.result = DLS_LIMIT; z.failure_region = DLS_REGIONS;
            memcpy(tmp, &z, sizeof(z)); expect_code(tmp, sizeof(tmp), 1);
            memcpy(tmp, ok, sizeof(ok)); put32(tmp, 20, (uint32_t)DLS_LIMIT); expect_code(tmp, sizeof(tmp), 1);
        }
        /* R1: a later stop cannot follow an already-failing profile check. */
        assert(sample(IO_AT, 400, tmp) == DLS_IO); put32(tmp, 64, 0x0c000000u); expect_code(tmp, sizeof(tmp), 1);
        assert(sample(IO_AT, 400, tmp) == DLS_IO); put32(tmp, 60 + 16 + 4, 0x8000u); expect_code(tmp, sizeof(tmp), 1);
        assert(sample(IO_AT, 400, tmp) == DLS_IO); put32(tmp, 60 + 24 + 4, 0x10000u); expect_code(tmp, sizeof(tmp), 1);
        assert(sample(LOCKED, 0, tmp) == DLS_UNSUPPORTED); put32(tmp, 64, 0x0c000000u); expect_code(tmp, sizeof(tmp), 1);
        assert(sample(HIGH_STREAMS, 0, tmp) == DLS_UNSUPPORTED); put32(tmp, 60 + 16 + 4, 0x8000u); expect_code(tmp, sizeof(tmp), 1);
        /* Partial-failure fields stay noncoherent: stale/unread words are not judged. */
        assert(sample(IO_AT, 400, tmp) == DLS_IO); put32(tmp, 24, 0); put32(tmp, 44, 0); expect_code(tmp, sizeof(tmp), 77);
        assert(sample(NOT_READY_AT, 7, tmp) == DLS_NOT_READY); put32(tmp, 24, 0); put32(tmp, 48, 0); expect_code(tmp, sizeof(tmp), 77);
        groups++;
    }

    /* 4. Missing provider / MIG failures: no record, COPY line, exit 1, no instance read. */
    {
        int states[3] = {DLC_GET_NO_ENTRY, DLC_GET_NO_PROPERTY, DLC_GET_ERROR};
        const char *verdicts[3] = {"verdict=MISSING", "verdict=MISSING", "verdict=MIG_ERROR"};
        for (unsigned i = 0; i < 3; i++) {
            struct fake f = {states[i], DLC_GET_OK, (int32_t)0xe00002be, 0, ok, sizeof(ok), ok, sizeof(ok), 0, 0};
            assert(run(&f) == 1 && f.pcalls == 1 && f.icalls == 0);
            assert(contains(verdicts[i]) && contains("DL13_DART_COPY") && !contains("RECORD_BEGIN"));
        }
        groups++;
    }

    /* 5. Optional instance copy: absent/equal keep the verdict; anything else fails. */
    {
        uint8_t other[DLC_DIAG_BYTES]; memcpy(other, ok, sizeof(ok)); other[700] ^= 1;
        struct fake eq = {DLC_GET_OK, DLC_GET_OK, 0, 0, ok, sizeof(ok), ok, sizeof(ok), 0, 0};
        assert(run(&eq) == 0 && eq.icalls == 1 && contains("instance=equal") && contains("PDDARTSurveyObservationV1"));
        assert(sample(NOT_READY_AT, 1, tmp) == DLS_NOT_READY);
        struct fake req = {DLC_GET_OK, DLC_GET_OK, 0, 0, tmp, sizeof(tmp), tmp, sizeof(tmp), 0, 0};
        assert(run(&req) == 77 && contains("instance=equal"));
        struct fake mm = {DLC_GET_OK, DLC_GET_OK, 0, 0, ok, sizeof(ok), other, sizeof(other), 0, 0};
        assert(run(&mm) == 1 && contains("verdict=INSTANCE_MISMATCH") && contains("instance=mismatch"));
        struct fake shorter = {DLC_GET_OK, DLC_GET_OK, 0, 0, ok, sizeof(ok), ok, sizeof(ok) - 1, 0, 0};
        assert(run(&shorter) == 1);
        int bad[2] = {DLC_GET_NO_PROPERTY, DLC_GET_ERROR};
        for (unsigned i = 0; i < 2; i++) {
            struct fake f = {DLC_GET_OK, bad[i], 0, 5, ok, sizeof(ok), NULL, 0, 0, 0};
            assert(run(&f) == 1 && contains("verdict=INSTANCE_MISMATCH") && contains("DL13_DART_COPY"));
        }
        groups++;
    }

    /* 6. Output failure is a failure. */
    {
        FILE *full = fopen("/dev/full", "w");
        if (full) {
            struct fake f = {DLC_GET_OK, DLC_GET_NO_ENTRY, 0, 0, ok, sizeof(ok), NULL, 0, 0, 0};
            char *a[] = {(char *)"x", (char *)"--nonce", (char *)NONCE, NULL};
            setvbuf(full, NULL, _IOFBF, 1 << 16);
            assert(dlc_run(3, a, fake_get, &f, full) == 1);
            fclose(full);
        }
        groups++;
    }

    /* Optional: dump representative outputs for the Python format decoder. */
    if (argc == 2) {
        struct fake cases[3] = {
            {DLC_GET_OK, DLC_GET_OK, 0, 0, ok, sizeof(ok), ok, sizeof(ok), 0, 0},
            {DLC_GET_OK, DLC_GET_NO_ENTRY, 0, 0, NULL, 0, NULL, 0, 0, 0},
            {DLC_GET_OK, DLC_GET_NO_ENTRY, 0, 0, ok, 100, NULL, 0, 0, 0}};
        uint8_t refused[DLC_DIAG_BYTES]; assert(sample(IO_AT, 400, refused) == DLS_IO);
        cases[1].p = refused; cases[1].pn = sizeof(refused);
        const char *names[3] = {"sampled.txt", "refused.txt", "malformed.txt"};
        int codes[3] = {0, 77, 1};
        for (unsigned i = 0; i < 3; i++) {
            assert(run(&cases[i]) == codes[i]);
            char path[4096]; snprintf(path, sizeof(path), "%s/%s", argv[1], names[i]);
            FILE *o = fopen(path, "w"); assert(o); fputs(last_output, o); fclose(o);
        }
        char path[4096]; snprintf(path, sizeof(path), "%s/sampled.bin", argv[1]);
        FILE *o = fopen(path, "wb"); assert(o); fwrite(ok, 1, sizeof(ok), o); fclose(o);
        snprintf(path, sizeof(path), "%s/refused.bin", argv[1]);
        o = fopen(path, "wb"); assert(o); fwrite(refused, 1, sizeof(refused), o); fclose(o);
    }
    free(last_output);
    printf("HOST_DART_COLLECTOR groups=%u sampler_snapshots=%u hardware=none\n", groups, snapshots);
    return 0;
}
