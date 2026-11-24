// SPDX-License-Identifier: GPL-2.0
/* See sf_nvme_part.h. No allocation, no I/O of its own, no dependency on the kernel. */
#include "sf_nvme_part.h"

#define GPT_ENTRIES   128u              /* what this disk has; anything else is refused */
#define GPT_ENTRY     128u
#define GPT_TABLE     (GPT_ENTRIES * GPT_ENTRY / SF_PART_BLOCK)         /* 4 blocks */
#define GPT_HEADER    92u

/* "Linux filesystem data" 0FC63DAF-8483-4772-8E79-3D69D8477DE4 as stored */
static const uint8_t kLinuxData[16] = { 0xaf, 0x3d, 0xc6, 0x0f, 0x83, 0x84, 0x72, 0x47, 0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4 };

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32; }

static int
same(const uint8_t *a, const uint8_t *b, size_t n)
{
	uint8_t d = 0;
	for (size_t i = 0; i < n; i++)
		d |= (uint8_t)(a[i] ^ b[i]);
	return d == 0;
}

static void
say(char *why, size_t len, const char *text)
{
	size_t i = 0;
	if (why == NULL || len == 0)
		return;
	for (; text[i] != 0 && i + 1 < len; i++)
		why[i] = text[i];
	why[i] = 0;
}

uint32_t
sf_part_crc32(const uint8_t *p, size_t n)
{
	uint32_t c = 0xffffffffu;
	for (size_t i = 0; i < n; i++) {
		c ^= p[i];
		for (int k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
	}
	return ~c;
}

static int
hex(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

int
sf_part_parse_guid(const char *text, uint8_t out[16])
{
	/* position in the text of each stored byte: the first three groups are little-endian */
	static const uint8_t at[16] = { 6, 4, 2, 0, 11, 9, 16, 14, 19, 21, 24, 26, 28, 30, 32, 34 };
	size_t n = 0;

	if (text == NULL)
		return 1;
	while (text[n] != 0 && n < 37)
		n++;
	if (n != 36 || text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-')
		return 1;
	for (unsigned i = 0; i < 16; i++) {
		int hi = hex(text[at[i]]), lo = hex(text[at[i] + 1]);
		if (hi < 0 || lo < 0)
			return 1;
		out[i] = (uint8_t)(hi << 4 | lo);
	}
	return 0;
}

/* One GPT copy: header at `self`, pointing at the other copy at `other`, its table at `table`. */
static int
header_ok(uint8_t *h, uint64_t self, uint64_t other, uint64_t table, uint64_t ns_blocks)
{
	uint32_t crc, stored;

	if (!same(h, (const uint8_t *)"EFI PART", 8) || rd32(h + 8) != 0x00010000u || rd32(h + 12) != GPT_HEADER)
		return 0;
	stored = rd32(h + 16);
	h[16] = h[17] = h[18] = h[19] = 0;
	crc = sf_part_crc32(h, GPT_HEADER);
	h[16] = (uint8_t)stored; h[17] = (uint8_t)(stored >> 8); h[18] = (uint8_t)(stored >> 16); h[19] = (uint8_t)(stored >> 24);
	if (crc != stored)
		return 0;
	if (rd64(h + 24) != self || rd64(h + 32) != other || rd64(h + 72) != table)
		return 0;
	if (rd32(h + 80) != GPT_ENTRIES || rd32(h + 84) != GPT_ENTRY)
		return 0;
	/* usable space: after the primary table, before the backup table */
	if (rd64(h + 40) < 2 + GPT_TABLE || rd64(h + 48) >= ns_blocks - 1 - GPT_TABLE || rd64(h + 40) > rd64(h + 48))
		return 0;
	return 1;
}

int
sf_part_verify(const struct sf_part_want *want, uint64_t ns_blocks, sf_part_read_t rd, void *ctx, uint8_t *work,
    char *why, size_t len)
{
	uint8_t *ph = work, *bh = work + SF_PART_BLOCK, *pt = work + 2 * SF_PART_BLOCK, *bt = pt + GPT_TABLE * SF_PART_BLOCK;
	uint64_t last, first_usable, last_usable;
	unsigned found = 0, index = 0;

	if (want == NULL || rd == NULL || work == NULL) { say(why, len, "no arguments"); return 1; }
	if (ns_blocks < 64 || want->blocks == 0 || want->first > UINT64_MAX - want->blocks ||
	    want->first + want->blocks > ns_blocks) { say(why, len, "the wanted range is not inside the disk"); return 2; }
	last = want->first + want->blocks - 1;

	if (rd(ctx, 1, 1, ph) != 0) { say(why, len, "primary GPT header unreadable"); return 3; }
	if (rd(ctx, ns_blocks - 1, 1, bh) != 0) { say(why, len, "backup GPT header unreadable"); return 4; }
	if (!header_ok(ph, 1, ns_blocks - 1, 2, ns_blocks)) { say(why, len, "primary GPT header invalid"); return 5; }
	if (!header_ok(bh, ns_blocks - 1, 1, ns_blocks - 1 - GPT_TABLE, ns_blocks)) { say(why, len, "backup GPT header invalid"); return 6; }
	/* the two headers describe the same disk: usable range, disk GUID, table checksum */
	if (!same(ph + 40, bh + 40, 16 + 16) || rd32(ph + 88) != rd32(bh + 88)) { say(why, len, "the GPT headers disagree"); return 7; }
	if (rd(ctx, 2, GPT_TABLE, pt) != 0) { say(why, len, "primary GPT table unreadable"); return 8; }
	if (rd(ctx, ns_blocks - 1 - GPT_TABLE, GPT_TABLE, bt) != 0) { say(why, len, "backup GPT table unreadable"); return 9; }
	if (sf_part_crc32(pt, GPT_ENTRIES * GPT_ENTRY) != rd32(ph + 88)) { say(why, len, "primary GPT table checksum"); return 10; }
	if (sf_part_crc32(bt, GPT_ENTRIES * GPT_ENTRY) != rd32(bh + 88)) { say(why, len, "backup GPT table checksum"); return 11; }
	if (!same(pt, bt, GPT_ENTRIES * GPT_ENTRY)) { say(why, len, "the GPT tables differ"); return 12; }

	first_usable = rd64(ph + 40);
	last_usable = rd64(ph + 48);
	for (unsigned i = 0; i < GPT_ENTRIES; i++)
		if (same(pt + i * GPT_ENTRY + 16, want->guid, 16)) {
			found++;
			index = i;
		}
	if (found != 1) { say(why, len, found == 0 ? "no partition has the wanted GUID" : "the wanted GUID occurs more than once"); return 13; }
	{
		const uint8_t *e = pt + index * GPT_ENTRY;

		if (!same(e, kLinuxData, 16)) { say(why, len, "the partition's type is not Linux filesystem data"); return 14; }
		if (rd64(e + 32) != want->first) { say(why, len, "the partition does not start at the wanted block"); return 15; }
		if (rd64(e + 40) != last) { say(why, len, "the partition does not have the wanted length"); return 16; }
		if (want->first < first_usable || last > last_usable) { say(why, len, "the partition is outside the usable range"); return 17; }
	}
	/* no other entry may share a block with it */
	for (unsigned i = 0; i < GPT_ENTRIES; i++) {
		const uint8_t *e = pt + i * GPT_ENTRY;
		static const uint8_t zero[16];

		if (i == index || same(e, zero, 16))
			continue;
		if (rd64(e + 32) <= last && rd64(e + 40) >= want->first) { say(why, len, "another partition overlaps it"); return 18; }
	}
	say(why, len, "found in both GPT copies");
	return 0;
}
