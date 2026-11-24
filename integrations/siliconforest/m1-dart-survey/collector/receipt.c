#include "receipt.h"
#include "sha256.h"
#include <string.h>

static void hex_bytes(FILE *out, const uint8_t *p, size_t n)
{
    static const char *digits = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; i++) {
        fputc(digits[p[i] >> 4], out);
        fputc(digits[p[i] & 0x0fu], out);
    }
}

static void hex_chunks(FILE *out, const uint8_t *bytes, size_t length)
{
    size_t off = 0;
    unsigned index = 0;
    while (off < length) {
        size_t n = length - off;
        if (n > DLS_HEX_CHUNK) n = DLS_HEX_CHUNK;
        fprintf(out, "hex %u %u %u ", index, (unsigned)off, (unsigned)n);
        hex_bytes(out, bytes + off, n);
        fputc('\n', out);
        off += n;
        index++;
    }
}

static const char *fetch_name(int fetch_rc)
{
    if (fetch_rc == DLS_FETCH_MISSING) return "missing";
    if (fetch_rc == DLS_FETCH_LENGTH) return "length";
    if (fetch_rc == DLS_FETCH_INSTANCE) return "instance";
    return "fetch";
}

static const char *instance_name(int instance)
{
    if (instance == DLS_INSTANCE_ABSENT) return "absent";
    if (instance == DLS_INSTANCE_EQUAL) return "equal";
    return "unread";
}

static void print_decoded(FILE *out, const struct dls_decoded *d)
{
    uint32_t i, shown;
    fprintf(out, "version %u\n", d->version);
    fprintf(out, "sample_count %u\n", d->sample_count);
    fprintf(out, "reads_attempted %u\n", d->reads_attempted);
    fprintf(out, "failure_region %u\n", d->failure_region);
    fprintf(out, "failure_offset %u\n", d->failure_offset);
    fprintf(out, "result %d\n", d->result);
    fprintf(out, "power %u %u %u %u\n", d->power[0], d->power[1], d->power[2], d->power[3]);
    fprintf(out, "clocks %u %u %u %u %u\n", d->clocks[0], d->clocks[1], d->clocks[2], d->clocks[3], d->clocks[4]);
    shown = d->sample_count;
    if (shown > DLS_SAMPLE_CAP) shown = DLS_SAMPLE_CAP;
    for (i = 0; i < shown; i++) {
        fprintf(out, "sample %u %u %u\n", i, d->sample_offset[i], d->sample_value[i]);
    }
}

int dls_emit(FILE *out, const char *nonce, const uint8_t *bytes, size_t length, int fetch_rc, int instance)
{
    uint8_t digest[32];
    struct dls_decoded decoded;
    int reason = DLS_REASON_NONE;
    int status = DLS_EXIT_FAIL;
    if (!out || !nonce) return DLS_EXIT_FAIL;
    fprintf(out, "nonce %s\n", nonce);
    if (fetch_rc != DLS_FETCH_OK && fetch_rc != DLS_FETCH_LENGTH && fetch_rc != DLS_FETCH_INSTANCE) {
        fprintf(out, "instance %s\n", instance_name(instance));
        fprintf(out, "error %s\n", fetch_name(fetch_rc));
        fprintf(out, "exit %d\n", DLS_EXIT_FAIL);
        return DLS_EXIT_FAIL;
    }
    if (!bytes || length > DLS_SNAPSHOT_BYTES) {
        fprintf(out, "instance %s\n", instance_name(instance));
        fprintf(out, "error length\n");
        fprintf(out, "exit %d\n", DLS_EXIT_FAIL);
        return DLS_EXIT_FAIL;
    }
    dls_sha256(bytes, length, digest);
    fprintf(out, "length %u\n", (unsigned)length);
    fprintf(out, "sha256 ");
    hex_bytes(out, digest, 32);
    fputc('\n', out);
    hex_chunks(out, bytes, length);
    if (length != DLS_SNAPSHOT_BYTES || fetch_rc == DLS_FETCH_LENGTH || dls_decode(bytes, length, &decoded) != 0) {
        fprintf(out, "instance %s\n", instance_name(instance));
        fprintf(out, "error length\n");
        fprintf(out, "exit %d\n", DLS_EXIT_FAIL);
        return DLS_EXIT_FAIL;
    }
    print_decoded(out, &decoded);
    fprintf(out, "instance %s\n", instance_name(instance));
    if (fetch_rc == DLS_FETCH_INSTANCE) {
        fprintf(out, "error instance\n");
        fprintf(out, "exit %d\n", DLS_EXIT_FAIL);
        return DLS_EXIT_FAIL;
    }
    status = dls_classify(&decoded, &reason);
    if (status == DLS_EXIT_FAIL) fprintf(out, "error %s\n", dls_reason_name(reason));
    fprintf(out, "exit %d\n", status);
    return status;
}
