#define _GNU_SOURCE
#include "classify.h"
#include "fetch.h"
#include "receipt.h"
#include "sha256.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void fill_prefix(uint8_t *b, uint32_t count)
{
    uint32_t i, n = count > DLS_SAMPLE_CAP ? DLS_SAMPLE_CAP : count;
    put_u32(b + 4, count);
    for (i = 0; i < n; i++) {
        uint32_t off = 0;
        assert(dls_prefix_offset(i, &off) == 0);
        put_u32(b + 60 + 8u * i, off);
        put_u32(b + 64 + 8u * i, 0xA000u + i);
    }
}

static void base_record(uint8_t *b, int32_t result, uint32_t count, uint32_t reads)
{
    memset(b, 0, DLS_SNAPSHOT_BYTES);
    put_u32(b + 0, 1u);
    put_u32(b + 8, reads);
    put_u32(b + 12, 5u);
    put_u32(b + 16, 0u);
    put_u32(b + 20, (uint32_t)result);
    put_u32(b + 24, 0x11u);
    put_u32(b + 28, 0x22u);
    put_u32(b + 32, 0x33u);
    put_u32(b + 36, 0x44u);
    put_u32(b + 40, 9u);
    put_u32(b + 44, 8u);
    put_u32(b + 48, 7u);
    put_u32(b + 52, 6u);
    put_u32(b + 56, 5u);
    fill_prefix(b, count);
}

static int classify_buf(const uint8_t *b, int *reason)
{
    struct dls_decoded d;
    assert(dls_decode(b, DLS_SNAPSHOT_BYTES, &d) == 0);
    return dls_classify(&d, reason);
}

static char *emit_text(const uint8_t *b, size_t n, int fetch_rc, int instance, int *status)
{
    char *text = 0;
    size_t len = 0;
    FILE *fp = open_memstream(&text, &len);
    assert(fp);
    *status = dls_emit(fp, "0123456789abcdef0123456789abcdef0123456789abcdef", b, n, fetch_rc, instance);
    assert(fclose(fp) == 0);
    return text;
}

static void expect_hex_digest(const uint8_t *data, size_t n, const char *hex)
{
    uint8_t dig[32], i;
    char out[65];
    dls_sha256(data, n, dig);
    for (i = 0; i < 32; i++) sprintf(out + 2 * i, "%02x", dig[i]);
    out[64] = 0;
    assert(strcmp(out, hex) == 0);
}

static void test_sha(void)
{
    uint8_t a55[55], a56[56], a63[63], a64[64], a65[65];
    memset(a55, 'a', sizeof(a55));
    memset(a56, 'a', sizeof(a56));
    memset(a63, 'a', sizeof(a63));
    memset(a64, 'a', sizeof(a64));
    memset(a65, 'a', sizeof(a65));
    expect_hex_digest(0, 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    expect_hex_digest((const uint8_t *)"abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    expect_hex_digest(a55, 55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    expect_hex_digest(a56, 56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    expect_hex_digest(a63, 63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34");
    expect_hex_digest(a64, 64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
    expect_hex_digest(a65, 65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0");
}

static void test_prefix(void)
{
    uint32_t off = 1;
    assert(dls_prefix_offset(0, &off) == 0 && off == 0u);
    assert(dls_prefix_offset(1, &off) == 0 && off == 4u);
    assert(dls_prefix_offset(2, &off) == 0 && off == 0x60u);
    assert(dls_prefix_offset(3, &off) == 0 && off == 0xfcu);
    assert(dls_prefix_offset(4, &off) == 0 && off == 0x80u);
    assert(dls_prefix_offset(7, &off) == 0 && off == 0x8cu);
    assert(dls_prefix_offset(8, &off) == 0 && off == 0x100u);
    assert(dls_prefix_offset(9, &off) == 0 && off == 0x200u);
    assert(dls_prefix_offset(12, &off) == 0 && off == 0x20cu);
    assert(dls_prefix_offset(13, &off) == 0 && off == 0x104u);
    assert(dls_prefix_offset(87, &off) == 0 && off == 0x2fcu);
    assert(dls_prefix_offset(88, &off) != 0);
}

static void test_nonce(void)
{
    char good[] = "0123456789abcdef0123456789abcdef0123456789abcdef";
    char upper[] = "0123456789abcdef0123456789ABCDEF0123456789abcdef";
    char *ok_argv[] = {"darwin-dart-survey", "--nonce", good};
    char *bad_flag[] = {"darwin-dart-survey", "--n", good};
    char *extra[] = {"darwin-dart-survey", "--nonce", good, "x"};
    const char *nonce = 0;
    assert(strlen(good) == 48);
    assert(dls_nonce_ok(good));
    assert(!dls_nonce_ok(upper));
    assert(!dls_nonce_ok("abc"));
    assert(!dls_nonce_ok(0));
    assert(dls_take_nonce(3, ok_argv, &nonce) == 0 && nonce == good);
    assert(dls_take_nonce(3, bad_flag, &nonce) == DLS_EXIT_USAGE);
    assert(dls_take_nonce(4, extra, &nonce) == DLS_EXIT_USAGE);
    ok_argv[2] = upper;
    assert(dls_take_nonce(3, ok_argv, &nonce) == DLS_EXIT_USAGE);
}

static void test_status(void)
{
    uint8_t b[DLS_SNAPSHOT_BYTES];
    int reason = -1;
    const int32_t refusals[] = {1, 2, -1, -2, -3, -4};
    unsigned i;
    base_record(b, 0, 88u, 889u);
    assert(classify_buf(b, &reason) == DLS_EXIT_SAMPLED && reason == DLS_REASON_NONE);
    for (i = 0; i < sizeof(refusals) / sizeof(refusals[0]); i++) {
        base_record(b, refusals[i], 3u, 40u);
        assert(classify_buf(b, &reason) == DLS_EXIT_REFUSAL);
    }
    base_record(b, 1, 0u, 0u);
    b[60] = 0xff; /* tail is not a sample and is not a validity bitmap */
    assert(classify_buf(b, &reason) == DLS_EXIT_REFUSAL);
    base_record(b, 0, 87u, 889u);
    assert(classify_buf(b, &reason) == DLS_EXIT_FAIL && reason == DLS_REASON_SAMPLED);
    base_record(b, 0, 88u, 888u);
    assert(classify_buf(b, &reason) == DLS_EXIT_FAIL && reason == DLS_REASON_SAMPLED);
    base_record(b, 0, 88u, 889u);
    put_u32(b + 12, 4u);
    assert(classify_buf(b, &reason) == DLS_EXIT_FAIL && reason == DLS_REASON_SAMPLED);
    base_record(b, 0, 88u, 889u);
    put_u32(b + 16, 1u);
    assert(classify_buf(b, &reason) == DLS_EXIT_FAIL && reason == DLS_REASON_SAMPLED);
    base_record(b, 1, 3u, 40u);
    put_u32(b + 60 + 16, 0x61u);
    assert(classify_buf(b, &reason) == DLS_EXIT_FAIL && reason == DLS_REASON_PREFIX);
    base_record(b, 1, 89u, 10u);
    assert(classify_buf(b, &reason) == DLS_EXIT_FAIL && reason == DLS_REASON_COUNT);
    base_record(b, 1, 0u, 1024u);
    assert(classify_buf(b, &reason) == DLS_EXIT_REFUSAL);
    base_record(b, 1, 0u, 1025u);
    assert(classify_buf(b, &reason) == DLS_EXIT_FAIL && reason == DLS_REASON_READS);
    base_record(b, 1, 0u, 0u);
    put_u32(b + 12, 6u);
    assert(classify_buf(b, &reason) == DLS_EXIT_FAIL && reason == DLS_REASON_REGION);
    base_record(b, 3, 0u, 0u);
    assert(classify_buf(b, &reason) == DLS_EXIT_FAIL && reason == DLS_REASON_RESULT);
    base_record(b, 1, 0u, 0u);
    put_u32(b + 0, 2u);
    assert(classify_buf(b, &reason) == DLS_EXIT_FAIL && reason == DLS_REASON_VERSION);
    {
        struct dls_decoded scratch;
        assert(dls_decode(b, 763, &scratch) != 0);
    }
}

static int line_ok(const char *text)
{
    const char *p = text;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        if (n > 96u || n >= DLS_SNAPSHOT_BYTES) return 0;
        if (!nl) return 0;
        p = nl + 1;
    }
    return 1;
}

static void rebuild_hex(const char *text, uint8_t *out, size_t expect)
{
    const char *p = text;
    size_t filled = 0;
    unsigned index = 0;
    while ((p = strstr(p, "hex ")) != 0) {
        unsigned got_index = 0, off = 0, n = 0;
        const char *hex;
        unsigned i;
        assert(sscanf(p, "hex %u %u %u", &got_index, &off, &n) == 3);
        assert(got_index == index);
        assert(n > 0 && n <= DLS_HEX_CHUNK);
        assert(off == filled);
        hex = strchr(p, '\n');
        assert(hex);
        hex -= (size_t)n * 2u;
        for (i = 0; i < n; i++) {
            unsigned byte = 0;
            assert(sscanf(hex + 2u * i, "%02x", &byte) == 1);
            out[filled + i] = (uint8_t)byte;
        }
        filled += n;
        index++;
        p = strchr(p, '\n');
        assert(p);
        p++;
    }
    assert(filled == expect);
    assert(index == (unsigned)((expect + DLS_HEX_CHUNK - 1u) / DLS_HEX_CHUNK));
}

static void test_receipt(void)
{
    uint8_t b[DLS_SNAPSHOT_BYTES], back[DLS_SNAPSHOT_BYTES], dig[32];
    char *text;
    char sha[65];
    int status = -1;
    unsigned i;
    const char *nonce = "0123456789abcdef0123456789abcdef0123456789abcdef";
    base_record(b, 0, 88u, 889u);
    text = emit_text(b, DLS_SNAPSHOT_BYTES, DLS_FETCH_OK, DLS_INSTANCE_ABSENT, &status);
    assert(status == DLS_EXIT_SAMPLED);
    assert(line_ok(text));
    assert(strstr(text, "error ") == 0);
    assert(strstr(text, "instance absent\n"));
    assert(strstr(text, "result 0\n"));
    assert(strstr(text, "sample_count 88\n"));
    assert(strstr(text, "reads_attempted 889\n"));
    assert(strstr(text, "failure_region 5\n"));
    assert(strstr(text, "failure_offset 0\n"));
    assert(strstr(text, "power 17 34 51 68\n"));
    assert(strstr(text, "exit 0\n"));
    rebuild_hex(text, back, DLS_SNAPSHOT_BYTES);
    assert(memcmp(back, b, DLS_SNAPSHOT_BYTES) == 0);
    dls_sha256(b, DLS_SNAPSHOT_BYTES, dig);
    for (i = 0; i < 32; i++) sprintf(sha + 2 * i, "%02x", dig[i]);
    sha[64] = 0;
    assert(strstr(text, sha));
    free(text);

    base_record(b, -2, 0u, 0u);
    text = emit_text(b, DLS_SNAPSHOT_BYTES, DLS_FETCH_OK, DLS_INSTANCE_EQUAL, &status);
    assert(status == DLS_EXIT_REFUSAL);
    assert(strstr(text, "result -2\n"));
    assert(strstr(text, "instance equal\n"));
    assert(strstr(text, "exit 77\n"));
    assert(strstr(text, "error ") == 0);
    rebuild_hex(text, back, DLS_SNAPSHOT_BYTES);
    assert(memcmp(back, b, DLS_SNAPSHOT_BYTES) == 0);
    free(text);

    base_record(b, 0, 88u, 889u);
    text = emit_text(b, DLS_SNAPSHOT_BYTES, DLS_FETCH_INSTANCE, DLS_INSTANCE_UNREAD, &status);
    assert(status == DLS_EXIT_FAIL);
    assert(strstr(text, "error instance\n"));
    assert(strstr(text, "sha256 "));
    rebuild_hex(text, back, DLS_SNAPSHOT_BYTES);
    free(text);

    text = emit_text(0, 0, DLS_FETCH_MISSING, DLS_INSTANCE_UNREAD, &status);
    assert(status == DLS_EXIT_FAIL);
    assert(strstr(text, "error missing\n"));
    assert(strstr(text, "hex ") == 0);
    assert(strstr(text, nonce));
    free(text);

    text = emit_text((const uint8_t *)"short-bytes", 11, DLS_FETCH_LENGTH, DLS_INSTANCE_UNREAD, &status);
    assert(status == DLS_EXIT_FAIL);
    assert(strstr(text, "error length\n"));
    assert(strstr(text, "length 11\n"));
    assert(line_ok(text));
    rebuild_hex(text, back, 11);
    assert(memcmp(back, "short-bytes", 11) == 0);
    free(text);

    text = emit_text(0, 0, DLS_FETCH_MIG, DLS_INSTANCE_UNREAD, &status);
    assert(status == DLS_EXIT_FAIL && strstr(text, "error fetch\n"));
    free(text);
}

static char *slurp(const char *name)
{
    FILE *fp = fopen(name, "rb");
    char *buf;
    long n;
    assert(fp);
    assert(fseek(fp, 0, SEEK_END) == 0);
    n = ftell(fp);
    assert(n > 0 && n < 200000);
    assert(fseek(fp, 0, SEEK_SET) == 0);
    buf = malloc((size_t)n + 1u);
    assert(buf);
    assert(fread(buf, 1, (size_t)n, fp) == (size_t)n);
    buf[n] = 0;
    assert(fclose(fp) == 0);
    return buf;
}

static int count_of(const char *hay, const char *needle)
{
    int c = 0;
    size_t n = strlen(needle);
    const char *p = hay;
    while ((p = strstr(p, needle)) != 0) {
        c++;
        p += n;
    }
    return c;
}

static void test_wrapper_boundary(void)
{
    static const char *files[] = {
        "classify.c", "classify.h", "receipt.c", "receipt.h", "sha256.c", "sha256.h",
        "fetch.h", "fetch_mig.c", "main.c"
    };
    static const char *banned[] = {
        "IOConnect", "IOServiceOpen", "IOHID", "mmap(", "CFDictionary", "setProperty", "/dev/"
    };
    char *mig;
    unsigned i, j;
    for (i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        char *text = slurp(files[i]);
        for (j = 0; j < sizeof(banned) / sizeof(banned[0]); j++) assert(strstr(text, banned[j]) == 0);
        free(text);
    }
    mig = slurp("fetch_mig.c");
    assert(strstr(mig, "#include <device_user.h>"));
    assert(strstr(mig, "-DIOKIT=1"));
    assert(strstr(mig, "-DKERNEL=0"));
    assert(strstr(mig, "-UKERNEL"));
    assert(count_of(mig, "io_registry_entry_get_property_bytes") == 1);
    assert(count_of(mig, "io_registry_entry_from_path") == 1);
    assert(count_of(mig, "io_service_get_matching_services") == 1);
    assert(count_of(mig, "PDDARTSurveyDiagnosticV1") == 1);
    assert(count_of(mig, "PDDARTSurveyObservationV1") == 1);
    assert(count_of(mig, "IOService:/IOResources") == 1);
    assert(count_of(mig, "kern_return_t io_registry_entry_get_property_bytes") == 0);
    free(mig);
}

int main(void)
{
    test_sha();
    test_prefix();
    test_nonce();
    test_status();
    test_receipt();
    test_wrapper_boundary();
    return 0;
}
