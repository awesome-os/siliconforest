/* SPDX-License-Identifier: MIT */
/* MD5 (RFC 1321), small and unoptimised: frame hashes of the decoder tests. */
#ifndef _SF_MD5_H
#define _SF_MD5_H

#include <stdint.h>
#include <string.h>

struct sf_md5 {
	uint32_t a, b, c, d;
	uint64_t bytes;
	uint8_t  block[64];
};

static inline uint32_t sf_md5_rol(uint32_t x, unsigned n) { return (x << n) | (x >> (32 - n)); }

static inline void
sf_md5_block(struct sf_md5 *m, const uint8_t *p)
{
	static const uint32_t k[64] = {
		0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
		0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
		0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
		0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
		0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
		0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
		0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
		0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
	};
	static const uint8_t s[64] = {
		7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
		4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
	};
	uint32_t w[16], a = m->a, b = m->b, c = m->c, d = m->d;

	for (int i = 0; i < 16; i++)
		w[i] = (uint32_t)p[4 * i] | ((uint32_t)p[4 * i + 1] << 8) | ((uint32_t)p[4 * i + 2] << 16) |
		    ((uint32_t)p[4 * i + 3] << 24);
	for (int i = 0; i < 64; i++) {
		uint32_t f;
		int g;

		if (i < 16) {
			f = (b & c) | (~b & d);
			g = i;
		} else if (i < 32) {
			f = (d & b) | (~d & c);
			g = (5 * i + 1) & 15;
		} else if (i < 48) {
			f = b ^ c ^ d;
			g = (3 * i + 5) & 15;
		} else {
			f = c ^ (b | ~d);
			g = (7 * i) & 15;
		}
		f += a + k[i] + w[g];
		a = d;
		d = c;
		c = b;
		b += sf_md5_rol(f, s[i]);
	}
	m->a += a;
	m->b += b;
	m->c += c;
	m->d += d;
}

static inline void
sf_md5_init(struct sf_md5 *m)
{
	m->a = 0x67452301;
	m->b = 0xefcdab89;
	m->c = 0x98badcfe;
	m->d = 0x10325476;
	m->bytes = 0;
}

static inline void
sf_md5_update(struct sf_md5 *m, const uint8_t *p, size_t n)
{
	size_t have = (size_t)(m->bytes & 63);

	m->bytes += n;
	if (have) {
		size_t take = 64 - have < n ? 64 - have : n;
		memcpy(m->block + have, p, take);
		p += take;
		n -= take;
		if (have + take < 64)
			return;
		sf_md5_block(m, m->block);
	}
	for (; n >= 64; p += 64, n -= 64)
		sf_md5_block(m, p);
	memcpy(m->block, p, n);
}

static inline void
sf_md5_final(struct sf_md5 *m, char hex[33])
{
	static const char digits[] = "0123456789abcdef";
	uint64_t bits = m->bytes * 8;
	uint8_t pad[72] = { 0x80 }, out[16];
	size_t have = (size_t)(m->bytes & 63), padlen = (have < 56 ? 56 : 120) - have;
	uint32_t v[4];

	for (int i = 0; i < 8; i++)
		pad[padlen + i] = (uint8_t)(bits >> (8 * i));
	sf_md5_update(m, pad, padlen + 8);
	v[0] = m->a;
	v[1] = m->b;
	v[2] = m->c;
	v[3] = m->d;
	for (int i = 0; i < 16; i++)
		out[i] = (uint8_t)(v[i / 4] >> (8 * (i % 4)));
	for (int i = 0; i < 16; i++) {
		hex[2 * i] = digits[out[i] >> 4];
		hex[2 * i + 1] = digits[out[i] & 15];
	}
	hex[32] = 0;
}

#endif
