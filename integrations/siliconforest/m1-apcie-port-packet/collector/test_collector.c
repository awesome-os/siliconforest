/* Original host-only tests. Actual unchanged apo_observe is the CPU oracle.
 * All read callbacks below return synthetic values; no MMIO/device is used. */
#define _GNU_SOURCE
#include "collector.h"
#include "observe_layout.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, cases;
#define CHECK(v)                                                               \
  do {                                                                         \
    checks++;                                                                  \
    if (!(v)) {                                                                \
      fprintf(stderr, "check failed %s:%d: %s\n", __FILE__, __LINE__, #v);     \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
struct fixture {
  unsigned calls, fail_at, gate, power_bad, final_bad;
  uint32_t extra, bad_word, phy_word;
};
static int fake_read(void *ctx, unsigned region, uint32_t off,
                     uint32_t *value) {
  struct fixture *f = ctx;
  unsigned initial = f->calls < 4;
  f->calls++;
  CHECK(apo_read_allowed(region, off));
  if (f->fail_at == f->calls)
    return -1;
  uint32_t v = 0;
  if (region == DLS_PMGR) {
    v = 0xff | f->extra;
    if ((initial && f->power_bad == f->calls) ||
        (!initial && f->final_bad &&
         off == (uint32_t[]){APO_PMGR_IMX, APO_PMGR_APCIE, APO_PMGR_APCIE_GP,
                             APO_PMGR_PCIE_REF}[f->final_bad - 1]))
      v = f->bad_word;
  } else if (region == DLS_PHY)
    v = f->gate == 2 ? f->phy_word : 0xf;
  else if (region == DLS_RC)
    v = f->gate == 3 ? 0 : 1;
  else if (off == APO_PORT_STATUS)
    v = f->gate == 4 ? 0 : 1;
  else if (off == APO_PORT_LINKSTS)
    v = 0; /* zero LINKSTS is still numeric OBSERVED */
  *value = v;
  return 0;
}
static struct dls_board board(void) {
  struct dls_board b = {0};
  b.base[DLS_PMGR] = UINT64_C(0x23b700000);
  b.base[DLS_RC] = UINT64_C(0x680000000);
  b.base[DLS_PHY] = UINT64_C(0x680080000);
  b.base[DLS_PORT] = UINT64_C(0x681000000);
  for (unsigned i = 0; i < 4; i++)
    b.size[i] = 0x4000;
  return b;
}
static void le_snapshot(const struct apo_snapshot *s, uint8_t *b) {
  /* Read struct representation with memcpy (no aliasing assumption), then
   * explicitly encode LE so the tests never rely on host byte order. */
  for (unsigned i = 0; i < 21; i++) {
    uint32_t w;
    memcpy(&w, (const uint8_t *)s + 4 * i, 4);
    for (unsigned j = 0; j < 4; j++)
      b[4 * i + j] = (uint8_t)(w >> (j * 8));
  }
}
struct copy {
  const uint8_t *raw;
  size_t n;
  int status;
  int32_t detail, cleanup;
  unsigned calls;
};
static int copy_raw(void *ctx, uint8_t *buf, size_t cap, size_t *n,
                    int32_t *detail, int32_t *cleanup) {
  struct copy *c = ctx;
  c->calls++;
  CHECK(cap == APC_CAP);
  if (c->status == 0 && c->n <= cap && c->n)
    memcpy(buf, c->raw, c->n);
  *n = c->n;
  *detail = c->detail;
  *cleanup = c->cleanup;
  return c->status;
}
static const char nonce[] = "0123456789abcdef0123456789abcdef0123456789abcdef";
static int run_copy(struct copy *c, char **text, size_t *len) {
  char *args[] = {"test", "--nonce", (char *)nonce, 0};
  FILE *f = open_memstream(text, len);
  CHECK(f);
  int rc = apc_run(3, args, copy_raw, c, f);
  CHECK(!fclose(f));
  CHECK(c->calls == 1);
  return rc;
}
static void save(const char *dir, const uint8_t *raw, size_t n,
                 const char *text, size_t len, int rc) {
  char path[2048];
  CHECK(snprintf(path, sizeof(path), "%s/case-%04u.txt", dir, cases) > 0);
  FILE *f = fopen(path, "wb");
  CHECK(f);
  CHECK(fwrite(text, 1, len, f) == len);
  CHECK(!fclose(f));
  CHECK(snprintf(path, sizeof(path), "%s/case-%04u.raw", dir, cases) > 0);
  f = fopen(path, "wb");
  CHECK(f);
  CHECK(fwrite(raw, 1, n, f) == n);
  CHECK(!fclose(f));
  CHECK(snprintf(path, sizeof(path), "%s/case-%04u.rc", dir, cases) > 0);
  f = fopen(path, "w");
  CHECK(f);
  CHECK(fprintf(f, "%d\n", rc) > 0);
  CHECK(!fclose(f));
  cases++;
}
static void validate_snapshot(const char *dir, struct apo_snapshot *s) {
  uint8_t raw[APC_BYTES], mutated[APC_BYTES];
  struct apc_summary out, original;
  le_snapshot(s, raw);
  memset(&out, 0xa5, sizeof(out));
  CHECK(!apc_validate(raw, sizeof(raw), &out));
  CHECK(out.result == s->result);
  CHECK(out.reads == s->reads_attempted);
  CHECK(out.valid == s->valid_mask);
  int expected = s->result == 0 ? 0 : (s->result > 0 ? 77 : 1);
  struct copy c = {raw, sizeof(raw), 0, 0, 0, 0};
  char *text = 0;
  size_t len = 0;
  CHECK(run_copy(&c, &text, &len) == expected);
  CHECK(strstr(text, "DL31_APCIE_RECORD_END") <
        strstr(text, "DL31_APCIE_VERDICT"));
  save(dir, raw, sizeof(raw), text, len, expected);
  free(text);
  /* Every metadata bit participates in the exact source prefix. Changing one
   * metadata bit while retaining all register values must fail closed. */
  for (unsigned field = 0; field < 7; field++)
    for (unsigned bit = 0; bit < 32; bit++) {
      memcpy(mutated, raw, sizeof(raw));
      mutated[field * 4 + bit / 8] ^= (uint8_t)(1u << (bit % 8));
      memset(&out, 0xa5, sizeof(out));
      original = out;
      int changed_result = (int32_t)((uint32_t)s->result ^ (1u << bit));
      if (field == 4 && !s->reads_attempted &&
          (changed_result == -1 || changed_result == -2 || changed_result == -3)) {
        /* The three zero-read source errors have identical non-result shape. */
        CHECK(!apc_validate(mutated, sizeof(mutated), &out));
        CHECK(out.result == changed_result);
      } else {
        CHECK(apc_validate(mutated, sizeof(mutated), &out) == -1);
        CHECK(!memcmp(&out, &original, sizeof(out)));
      }
    }
  static const unsigned fields[14] = {7,  8,  9,  10, 15, 16, 17,
                                      18, 19, 20, 11, 12, 13, 14};
  for (unsigned bit = 0; bit < 14; bit++)
    if (!(s->valid_mask & (1u << bit))) {
      memcpy(mutated, raw, sizeof(raw));
      mutated[4 * fields[bit]] = 1;
      CHECK(apc_validate(mutated, sizeof(mutated), &out) == -1);
    }
  for (size_t n = 0; n < APC_BYTES; n++)
    CHECK(apc_validate(raw, n, &out) == -1);
  CHECK(apc_validate(raw, APC_BYTES + 1, &out) == -1);
  CHECK(apc_validate(0, sizeof(raw), &out) == -1);
  CHECK(apc_validate(raw, sizeof(raw), 0) == -1);
}
static void case_fixture(const char *dir, struct fixture source) {
  struct apo_snapshot s;
  struct dls_board b = board();
  struct dls_io io = {&source, fake_read};
  int result = apo_observe(&b, &io, &s);
  CHECK(result == s.result);
  CHECK(source.calls == s.reads_attempted);
  validate_snapshot(dir, &s);
}
static ssize_t broken_write(void *cookie, const char *bytes, size_t n) {
  (void)cookie;
  (void)bytes;
  (void)n;
  errno = EPIPE;
  return -1;
}
int main(int argc, char **argv) {
  CHECK(argc == 2);
  const char *dir = argv[1];
  struct apo_snapshot s;
  struct dls_board b = board();
  struct fixture f = {0};
  struct dls_io io = {&f, fake_read};
  CHECK(apo_observe(0, &io, &s) == -1);
  validate_snapshot(dir, &s);
  b.base[0]++;
  CHECK(apo_observe(&b, &io, &s) == -2);
  validate_snapshot(dir, &s);
  /* Actual adapter's pre-observer map/property failure has this zero shape. */
  s.result = -3;
  validate_snapshot(dir, &s);
  for (unsigned gate = 0; gate <= 4; gate++)
    if (gate != 1) {
      struct fixture base = {.gate = gate};
      case_fixture(dir, base);
      unsigned total = gate == 2 ? 9 : (gate == 3 ? 10 : (gate == 4 ? 13 : 14));
      for (unsigned fail = 1; fail <= total; fail++) {
        base.fail_at = fail;
        case_fixture(dir, base);
      }
      base.fail_at = 0;
      for (unsigned final = 1; final <= 4; final++) {
        base.final_bad = final;
        case_fixture(dir, base);
      }
    }
  for (unsigned i = 1; i <= 4; i++)
    case_fixture(dir, (struct fixture){.power_bad = i});
  for (unsigned bit = 0; bit < 32; bit++) {
    if ((UINT32_C(0x90001c00) >> bit) & 1u) {
      case_fixture(dir, (struct fixture){.power_bad = 1,
                                        .bad_word = 0xffu | (1u << bit)});
      case_fixture(dir, (struct fixture){.final_bad = 1,
                                        .bad_word = 0xffu | (1u << bit)});
    }
    if (bit < 8)
      case_fixture(dir, (struct fixture){.power_bad = 1,
                                        .bad_word = 0xffu ^ (1u << bit)});
  }
  for (unsigned bit = 0; bit < 8; bit++)
    case_fixture(dir, (struct fixture){.gate = 2,
                                      .phy_word = 0xfu ^ (1u << bit)});
  case_fixture(dir,
               (struct fixture){
                   .extra = 0x10000}); /* irrelevant power bits remain data */
  CHECK(apc_nonce_valid(nonce));
  CHECK(!apc_nonce_valid(0));
  CHECK(!apc_nonce_valid(""));
  CHECK(!apc_nonce_valid("A123456789abcdef0123456789abcdef0123456789abcdef"));
  uint8_t zero[APC_CAP] = {0};
  char *text = 0;
  size_t len = 0;
  for (unsigned n = 0; n <= APC_CAP; n += (n < 100 ? 1 : 53)) {
    struct copy c = {zero, n, 0, 0, 0, 0};
    CHECK(run_copy(&c, &text, &len) == 1);
    save(dir, zero, n, text, len, 1);
    free(text);
    text = 0;
  }
  struct copy c = {zero, APC_CAP, 0, 0, 0, 0};
  CHECK(run_copy(&c, &text, &len) == 1);
  save(dir, zero, APC_CAP, text, len, 1);
  free(text);
  text = 0;
  const int statuses[] = {APC_GET_MISSING, APC_GET_RPC, 12};
  for (unsigned i = 0; i < 3; i++) {
    c = (struct copy){zero, 0, statuses[i], -123, -7, 0};
    CHECK(run_copy(&c, &text, &len) == 1);
    save(dir, zero, 0, text, len, 1);
    free(text);
    text = 0;
  }
  c = (struct copy){zero, APC_CAP + 1, 0, 0, 0, 0};
  CHECK(run_copy(&c, &text, &len) == 1);
  save(dir, zero, 0, text, len, 1);
  free(text);
  text = 0;
  b = board();
  f = (struct fixture){0};
  CHECK(!apo_observe(&b, &io, &s));
  uint8_t raw[APC_BYTES];
  le_snapshot(&s, raw);
  c = (struct copy){raw, sizeof(raw), 0, 0, -7, 0};
  CHECK(run_copy(&c, &text, &len) == 1);
  save(dir, raw, sizeof(raw), text, len, 1);
  free(text);
  char *args[] = {"test", "--nonce", (char *)nonce, 0};
  FILE *sink = tmpfile();
  CHECK(sink);
  c = (struct copy){raw, sizeof(raw), 0, 0, 0, 0};
  CHECK(apc_run(2, args, copy_raw, &c, sink) == 2);
  CHECK(!c.calls);
  args[1] = "--other";
  CHECK(apc_run(3, args, copy_raw, &c, sink) == 2);
  CHECK(!c.calls);
  CHECK(!fclose(sink));
  args[1] = "--nonce";
  cookie_io_functions_t funcs = {.write = broken_write};
  sink = fopencookie(0, "w", funcs);
  CHECK(sink);
  setbuf(sink, 0);
  CHECK(apc_run(3, args, copy_raw, &c, sink) == 1);
  CHECK(c.calls == 1);
  (void)fclose(sink);
  fprintf(stderr,
          "PASS actual-source synthetic cases=%u assertions=%u no hardware\n",
          cases, checks);
  return 0;
}
