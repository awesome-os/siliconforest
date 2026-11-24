/* SPDX-License-Identifier: MIT */
/* Exercise the staged shared library independently of the older PD core ABI.
 * Vectors: FIPS 180-4 SHA-256, RFC 4231 HMAC test case 1, FIPS 197 AES. */
#include "ccdigest.h"
#include "cchmac.h"
#include "ccmode.h"
#include "ccrng.h"
struct ccrng_state *ccrng(int *error);
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    static const unsigned char sha[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    static const unsigned char hmac[32] = {
        0xb0,0x34,0x4c,0x61,0xd8,0xdb,0x38,0x53,0x5c,0xa8,0xaf,0xce,0xaf,0x0b,0xf1,0x2b,
        0x88,0x1d,0xc2,0x00,0xc9,0x83,0x3d,0xa7,0x26,0xe9,0x37,0x6c,0x2e,0x32,0xcf,0xf7};
    static const unsigned char plain[16] = {
        0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
    static const unsigned char encrypted[3][16] = {
        {0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a},
        {0xdd,0xa9,0x7c,0xa4,0x86,0x4c,0xdf,0xe0,0x6e,0xaf,0x70,0xa0,0xec,0x0d,0x71,0x91},
        {0x8e,0xa2,0xb7,0xca,0x51,0x67,0x45,0xbf,0xea,0xfc,0x49,0x90,0x4b,0x49,0x60,0x89}};
    unsigned char out[64], key[32], back[16], random[64];
    const struct ccdigest_info *di = ccsha256_di();
    assert(di && di->output_size == 32);
    ccdigest(di, 3, "abc", out);
    assert(!memcmp(out, sha, 32));
    _Alignas(16) unsigned char state[256];
    assert(ccdigest_di_size(di) <= sizeof(state));
    ccdigest_init(di, state);
    ccdigest_update(di, state, 1, "a");
    ccdigest_update(di, state, 2, "bc");
    di->final(di, state, out);
    assert(!memcmp(out, sha, 32));
    memset(key, 0x0b, 20);
    cchmac(di, 20, key, 8, "Hi There", out);
    assert(!memcmp(out, hmac, 32));
    for (size_t i=0; i<sizeof(key); ++i) key[i]=(unsigned char)i;
    for (size_t n=16; n<=32; n+=8) {
        assert(!ccecb_one_shot(ccaes_ecb_encrypt_mode(), n, key, 1, plain, out));
        assert(!memcmp(out, encrypted[(n-16)/8], 16));
        assert(!ccecb_one_shot(ccaes_ecb_decrypt_mode(), n, key, 1, out, back));
        assert(!memcmp(back, plain, 16));
    }
    int error=-1;
    struct ccrng_state *rng = ccrng(&error);
    assert(rng && !error && !rng->generate(rng, sizeof(random), random));
    memset(out, 0, sizeof(out));
    assert(memcmp(random, out, sizeof(out)));
    puts("corecrypto known vectors: PASS (SHA256 streaming, HMAC, AES128/192/256, RNG)");
    return 0;
}
