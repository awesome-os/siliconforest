/* Original source; license selection pending. No transport or hardware access. */
#include "envelope.h"
#define KNOWN_META UINT64_C(0x01ffff00000000ff)
static int span(const void *p, size_t n)
{
    return p && n && (uintptr_t)p <= UINTPTR_MAX - (n - 1);
}
static int overlap(const void *a, size_t an, const void *b, size_t bn)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x <= y ? y - x < an : x - y < bn;
}
static uint64_t get64(const uint8_t *p)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (8u * i);
    return v;
}
static void put64(uint8_t *p, uint64_t v)
{
    for (unsigned i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8u * i));
}
static enum ace_code inputs(enum ace_profile p, const uint8_t *b, size_t n,
                            void *out, size_t z)
{
    if (p != ACE_PIN121FCBF_LEGACY) return ACE_PROFILE;
    if (n != ACE_BYTES) return ACE_LENGTH;
    if (!span(b, n) || !span(out, z)) return ACE_ARGUMENT;
    if (overlap(b, n, out, z)) return ACE_OVERLAP;
    return ACE_OK;
}
enum ace_code ace_decode_rx(enum ace_profile p, const uint8_t *b, size_t n,
                            struct ace_rx *out)
{
    enum ace_code c = inputs(p, b, n, out, sizeof(*out));
    if (c != ACE_OK) return c;
    uint64_t m = get64(b + 8);
    struct ace_rx r = {get64(b), m, m & ~KNOWN_META, m & 255,
                       (m >> 52) & 31, (m >> 48) & 15,
                       (m >> 44) & 15, (m >> 40) & 15};
    *out = r;
    return ACE_OK;
}
enum ace_code ace_encode_tx(enum ace_profile p, uint32_t ep, uint64_t word,
                            uint8_t *out, size_t n)
{
    if (p != ACE_PIN121FCBF_LEGACY) return ACE_PROFILE;
    if (n != ACE_BYTES) return ACE_LENGTH;
    if (!span(out, n)) return ACE_ARGUMENT;
    if (ep > 255) return ACE_ENDPOINT;
    put64(out, word);
    put64(out + 8, ep);
    return ACE_OK;
}
enum ace_code ace_rtkit_input(enum ace_profile p, const uint8_t *b, size_t n,
                              struct ace_message *out)
{
    enum ace_code c = inputs(p, b, n, out, sizeof(*out));
    if (c != ACE_OK) return c;
    uint64_t m = get64(b + 8);
    if (m & UINT64_C(0xffffff00)) return ACE_METADATA;
    struct ace_message r = {get64(b), m & 255};
    *out = r;
    return ACE_OK;
}
