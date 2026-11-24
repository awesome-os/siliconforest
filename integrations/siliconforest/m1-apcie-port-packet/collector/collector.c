/* Original strict collector: bytes and fixed ASCII only, no registry API here.
 */
#include "collector.h"
#include <string.h>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/sha.h>
#endif

struct replay {
  const uint8_t *raw;
  uint32_t mask, reads, stage, region, offset;
};
static const uint32_t power_offsets[4] = {0x1b8, 0x348, 0x3e8, 0x1a0};
static const unsigned word_for_bit[14] = {7,  8,  9,  10, 15, 16, 17,
                                          18, 19, 20, 11, 12, 13, 14};
static uint32_t word(const uint8_t *b, unsigned i) {
  b += 4u * i;
  return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 |
         (uint32_t)b[3] << 24;
}
static int on(uint32_t v) {
  return !(v & UINT32_C(0x90001c00)) && (v & 255u) == 255u;
}
static int take(struct replay *r, unsigned bit, unsigned region,
                uint32_t offset) {
  r->reads++;
  r->region = region;
  r->offset = offset;
  if (!(word(r->raw, 3) & (1u << bit)))
    return -1;
  r->mask |= 1u << bit;
  return 0;
}
static int finish(struct replay *r, int result, struct apc_summary *out) {
  const uint8_t *b = r->raw;
  if (word(b, 1) != r->reads || word(b, 2) != r->stage ||
      word(b, 3) != r->mask || word(b, 4) != (uint32_t)result ||
      word(b, 5) != r->region || word(b, 6) != r->offset)
    return -1;
  for (unsigned i = 0; i < 14; i++)
    if (!(r->mask & (1u << i)) && word(b, word_for_bit[i]))
      return -1;
  struct apc_summary s = {r->reads,  r->stage,  r->mask,
                          r->region, r->offset, result};
  *out = s;
  return 0;
}
int apc_validate(const uint8_t *b, size_t n, struct apc_summary *out) {
  if (!b || !out || n != APC_BYTES || word(b, 0) != 1 || word(b, 1) > 14 ||
      word(b, 2) > 5 || word(b, 3) & ~0x3fffu)
    return -1;
  struct replay r = {b, 0, 0, 0, 5, 0};
  int result = 0;
  if (word(b, 1) == 0) {
    if (word(b, 4) == (uint32_t)-1)
      result = -1;
    else if (word(b, 4) == (uint32_t)-2)
      result = -2;
    else if (word(b, 4) == (uint32_t)-3)
      result = -3;
    else
      return -1;
    return finish(&r, result, out);
  }
  r.stage = 1;
  for (unsigned i = 0; i < 4; i++) {
    if (take(&r, i, 0, power_offsets[i]))
      return finish(&r, -3, out);
    if (!on(word(b, 7 + i)))
      return finish(&r, 1, out);
  }
  r.stage = 2;
  if (take(&r, 4, 2, 0))
    return finish(&r, -3, out);
  if ((word(b, 15) & 0x8fu) != 15u) {
    result = 2;
    goto final;
  }
  r.stage = 3;
  if (take(&r, 5, 1, 0x24))
    return finish(&r, -3, out);
  if (!(word(b, 16) & 1u)) {
    result = 3;
    goto final;
  }
  r.stage = 4;
  if (take(&r, 6, 3, 0x800))
    return finish(&r, -3, out);
  if (take(&r, 7, 3, 0x814))
    return finish(&r, -3, out);
  if (take(&r, 8, 3, 0x804))
    return finish(&r, -3, out);
  if (!(word(b, 19) & 1u)) {
    result = 4;
    goto final;
  }
  r.stage = 5;
  if (take(&r, 9, 3, 0x208))
    return finish(&r, -3, out);
final:
  for (unsigned i = 0; i < 4; i++) {
    if (take(&r, 10 + i, 0, power_offsets[i]))
      return finish(&r, -3, out);
    if (!on(word(b, 11 + i)))
      return finish(&r, 5, out);
  }
  return finish(&r, result, out);
}
int apc_nonce_valid(const char *s) {
  if (!s || strlen(s) != 48)
    return 0;
  for (unsigned i = 0; i < 48; i++)
    if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
      return 0;
  return 1;
}
static int emit_raw(FILE *f, const char *nonce, const uint8_t *b, size_t n) {
  static const char hex[] = "0123456789abcdef";
  unsigned char digest[32];
  char digest_hex[65], line[129];
#ifdef __APPLE__
  if (!CC_SHA256(b, (CC_LONG)n, digest))
    return -1;
#else
  if (!SHA256(b, n, digest))
    return -1;
#endif
  for (unsigned i = 0; i < 32; i++) {
    digest_hex[2 * i] = hex[digest[i] >> 4];
    digest_hex[2 * i + 1] = hex[digest[i] & 15];
  }
  digest_hex[64] = 0;
  fprintf(f,
          "DL31_APCIE_RECORD_BEGIN %s " APC_PROPERTY
          " bytes=%zu chunk_bytes=64\n",
          nonce, n);
  size_t index = 0;
  for (size_t offset = 0; offset < n; offset += 64) {
    size_t count = n - offset < 64 ? n - offset : 64;
    for (size_t i = 0; i < count; i++) {
      line[2 * i] = hex[b[offset + i] >> 4];
      line[2 * i + 1] = hex[b[offset + i] & 15];
    }
    line[2 * count] = 0;
    fprintf(f, "DL31_APCIE_RECORD_HEX %s " APC_PROPERTY " index=%zu %s\n",
            nonce, index++, line);
  }
  fprintf(f,
          "DL31_APCIE_RECORD_END %s " APC_PROPERTY
          " bytes=%zu chunks=%zu sha256=%s\n",
          nonce, n, index, digest_hex);
  return fflush(f) || ferror(f) ? -1 : 0;
}
int apc_run(int argc, char **argv, apc_getter get, void *context, FILE *out) {
  uint8_t raw[APC_CAP] = {0};
  size_t n = 0;
  int32_t detail = 0, cleanup = 0;
  struct apc_summary summary;
  if (argc != 3 || !argv || !argv[1] || !argv[2] ||
      strcmp(argv[1], "--nonce") || !apc_nonce_valid(argv[2]) || !get || !out)
    return 2;
  const char *nonce = argv[2];
  int status = get(context, raw, sizeof(raw), &n, &detail, &cleanup);
  const char *verdict = "RPC_ERROR";
  int code = 1;
  if (status == APC_GET_OK && n <= APC_CAP) {
    /* Actual bounded returned bytes and digest are flushed BEFORE validation.
     */
    if (emit_raw(out, nonce, raw, n))
      return 1;
    if (cleanup)
      verdict = "CLEANUP_ERROR";
    else if (apc_validate(raw, n, &summary))
      verdict = "MALFORMED";
    else {
      if (summary.result == 0) {
        verdict = "OBSERVED";
        code = 0;
      } else if (summary.result >= 1 && summary.result <= 5) {
        verdict = "NOT_READY";
        code = 77;
      } else
        verdict = "SOURCE_ERROR";
      fprintf(out,
              "DL31_APCIE_VERDICT %s verdict=%s result=%d reads=%u stage=%u "
              "valid=%u region=%u offset=%u initialization_proven=false\n",
              nonce, verdict, (int)summary.result, summary.reads, summary.stage,
              summary.valid, summary.region, summary.offset);
      return fflush(out) || ferror(out) ? 1 : code;
    }
  } else {
    if (status == APC_GET_MISSING)
      verdict = "MISSING";
    fprintf(out, "DL31_APCIE_COPY %s status=%s detail=%d cleanup=%d\n", nonce,
            status == APC_GET_MISSING ? "missing" : "rpc_error", (int)detail,
            (int)cleanup);
  }
  fprintf(out, "DL31_APCIE_VERDICT %s verdict=%s initialization_proven=false\n",
          nonce, verdict);
  return fflush(out) || ferror(out) ? 1 : code;
}
