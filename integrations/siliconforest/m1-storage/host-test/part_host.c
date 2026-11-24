// SPDX-License-Identifier: GPL-2.0
/*
 * Host test of kernel/sf_nvme_part.c: the identification of the SiliconForest partition in
 * both GPT copies. A synthetic disk with the shape of the real one (same block size, same
 * table geometry, eight partitions), then every way in which it can be wrong.
 *   part_host                      the synthetic cases
 *   part_host <head> <tail> <ns_blocks> <guid> <first> <blocks>
 *                                  a real disk's first and last 6 blocks, read elsewhere
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/sf_nvme_part.h"

#define NS 244276265ull
#define B SF_PART_BLOCK
static uint8_t head[6 * B], tail[6 * B];        /* blocks 0..5 and NS-6..NS-1 */
static uint64_t ns = NS;
static unsigned reads, fail_read_at;

static int
rd(void *ctx, uint64_t lba, uint32_t blocks, uint8_t *out)
{
	(void)ctx;
	if (++reads == fail_read_at)
		return 5;
	if (lba + blocks <= 6) { memcpy(out, head + lba * B, (size_t)blocks * B); return 0; }
	if (lba >= ns - 6 && lba + blocks <= ns) { memcpy(out, tail + (lba - (ns - 6)) * B, (size_t)blocks * B); return 0; }
	return 5;                                       /* the verification must read nothing else */
}

static void w32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void w64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }

static const char *kOurs = "5f3a9c1e-7b24-4d86-a1f0-3c9e2b7d4a65";      /* made up for the test */
static const uint64_t kFirst = 192633862ull, kBlocks = 16035840ull;
static const uint8_t kLinux[16] = { 0xaf, 0x3d, 0xc6, 0x0f, 0x83, 0x84, 0x72, 0x47, 0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4 };
static const uint8_t kApfs[16] = { 0xef, 0x57, 0x34, 0x7c, 0x00, 0x00, 0xaa, 0x11, 0xaa, 0x11, 0x00, 0x30, 0x65, 0x43, 0xec, 0xac };

static uint8_t *ptab(void) { return head + 2 * B; }
static uint8_t *btab(void) { return tail + B; }
static uint8_t *phdr(void) { return head + B; }
static uint8_t *bhdr(void) { return tail + 5 * B; }

static void
entry(unsigned i, const uint8_t *type, uint8_t tag, uint64_t first, uint64_t blocks)
{
	uint8_t *e = ptab() + i * 128;
	memcpy(e, type, 16);
	memset(e + 16, tag, 16);
	w64(e + 32, first);
	w64(e + 40, first + blocks - 1);
}

static void
seal(void)      /* copies the table to the backup and writes both headers with their checksums */
{
	for (int copy = 0; copy < 2; copy++) {
		uint8_t *h = copy ? bhdr() : phdr();
		memset(h, 0, B);
		memcpy(h, "EFI PART", 8);
		w32(h + 8, 0x00010000); w32(h + 12, 92);
		w64(h + 24, copy ? ns - 1 : 1); w64(h + 32, copy ? 1 : ns - 1);
		w64(h + 40, 6); w64(h + 48, ns - 6 - 1);
		memset(h + 56, 0x77, 16);
		w64(h + 72, copy ? ns - 5 : 2);
		w32(h + 80, 128); w32(h + 84, 128);
		w32(h + 88, sf_part_crc32(copy ? btab() : ptab(), 128 * 128));
		w32(h + 16, sf_part_crc32(h, 92));
	}
}
static void reseal_header(uint8_t *h) { w32(h + 16, 0); w32(h + 16, sf_part_crc32(h, 92)); }

static void
good(void)
{
	uint8_t g[16];
	memset(head, 0, sizeof(head)); memset(tail, 0, sizeof(tail)); ns = NS;
	entry(0, kApfs, 1, 6, 128000);
	entry(1, kApfs, 2, 128006, 192505856ull);
	entry(2, kLinux, 0, kFirst, kBlocks);
	if (sf_part_parse_guid(kOurs, g) != 0) abort();
	memcpy(ptab() + 2 * 128 + 16, g, 16);
	entry(3, kApfs, 4, 209411119ull, 610304);
	entry(4, kLinux, 5, 210021423ull, 128000);       /* stands for the EFI partition */
	entry(5, kLinux, 6, 210149423ull, 524288);       /* another Linux-typed partition */
	entry(6, kLinux, 7, 210673711ull, 32291840ull);
	entry(7, kApfs, 8, 242965551ull, 1310713);
	memcpy(btab(), ptab(), 128 * 128);
	seal();
}

static unsigned failures, cases;
static void
expect(const char *name, int want_ok, const char *guid, uint64_t first, uint64_t blocks)
{
	struct sf_part_want w;
	static uint8_t work[SF_PART_WORK];
	char why[96] = "";
	int r;

	cases++;
	memset(&w, 0, sizeof(w));
	if (sf_part_parse_guid(guid, w.guid) != 0) {
		r = -1; snprintf(why, sizeof(why), "GUID text refused");
	} else {
		w.first = first; w.blocks = blocks;
		reads = 0;
		r = sf_part_verify(&w, ns, rd, NULL, work, why, sizeof(why));
	}
	if ((r == 0) != (want_ok != 0)) { failures++; printf("PARTHOST FAIL %s: result %d (%s)\n", name, r, why); }
	else printf("PARTHOST ok   %-44s %s%s\n", name, r == 0 ? "accepted: " : "refused: ", why);
}
#define BAD(name) expect(name, 0, kOurs, kFirst, kBlocks)

int
main(int argc, char **argv)
{
	if (argc == 7) {
		FILE *f = fopen(argv[1], "rb"), *g = fopen(argv[2], "rb");
		if (!f || !g || fread(head, 1, sizeof(head), f) != sizeof(head) || fread(tail, 1, sizeof(tail), g) != sizeof(tail)) {
			printf("PARTHOST FAIL cannot read the two files (6 blocks each)\n"); return 1;
		}
		ns = strtoull(argv[3], NULL, 10);
		expect("the real disk's GPT copies", 1, argv[4], strtoull(argv[5], NULL, 10), strtoull(argv[6], NULL, 10));
		expect("real disk, start one block early", 0, argv[4], strtoull(argv[5], NULL, 10) - 1, strtoull(argv[6], NULL, 10));
		expect("real disk, one block longer", 0, argv[4], strtoull(argv[5], NULL, 10), strtoull(argv[6], NULL, 10) + 1);
		expect("real disk, another GUID", 0, kOurs, strtoull(argv[5], NULL, 10), strtoull(argv[6], NULL, 10));
		printf(failures ? "PARTHOST REAL FAILED\n" : "PARTHOST REAL PASSED\n");
		return failures != 0;
	}

	good(); expect("the partition as it is", 1, kOurs, kFirst, kBlocks);
	expect("the same GUID in capitals", 1, "5F3A9C1E-7B24-4D86-A1F0-3C9E2B7D4A65", kFirst, kBlocks);
	expect("GUID text one character short", 0, "5f3a9c1e-7b24-4d86-a1f0-3c9e2b7d4a6", kFirst, kBlocks);
	expect("GUID text with a wrong separator", 0, "5f3a9c1e_7b24-4d86-a1f0-3c9e2b7d4a65", kFirst, kBlocks);
	expect("GUID text with a non-hex digit", 0, "5f3a9c1e-7b24-4d86-a1f0-3c9e2b7d4a6g", kFirst, kBlocks);
	expect("another GUID", 0, "5f3a9c1e-7b24-4d86-a1f0-3c9e2b7d4a66", kFirst, kBlocks);
	expect("an all-zero GUID (matches no used entry)", 0, "00000000-0000-0000-0000-000000000000", kFirst, kBlocks);
	expect("start one block early", 0, kOurs, kFirst - 1, kBlocks);
	expect("start one block late", 0, kOurs, kFirst + 1, kBlocks);
	expect("one block shorter", 0, kOurs, kFirst, kBlocks - 1);
	expect("one block longer", 0, kOurs, kFirst, kBlocks + 1);
	expect("zero length", 0, kOurs, kFirst, 0);
	expect("length that wraps", 0, kOurs, kFirst, ~0ull);
	expect("range past the end of the disk", 0, kOurs, NS - 4, 8);
	/* the geometry of another partition with our GUID asked for: refused; and the reverse */
	expect("our GUID with the Linux root's range", 0, kOurs, 210673711ull, 32291840ull);
	expect("another Linux partition's GUID and range", 0, "07070707-0707-0707-0707-070707070707", kFirst, kBlocks);

	good(); ptab()[2 * 128] ^= 1; memcpy(btab(), ptab(), 128 * 128); seal(); BAD("type GUID is not Linux filesystem data");
	good(); memcpy(ptab() + 2 * 128, kApfs, 16); memcpy(btab(), ptab(), 128 * 128); seal(); BAD("type GUID is APFS");
	good(); w64(ptab() + 2 * 128 + 32, kFirst + 1); seal(); BAD("primary table moved the start, checksum made right");
	good(); w64(btab() + 2 * 128 + 32, kFirst + 1); w32(bhdr() + 88, sf_part_crc32(btab(), 16384)); reseal_header(bhdr()); BAD("backup table moved the start, checksum made right");
	good(); w64(btab() + 2 * 128 + 40, kFirst + kBlocks); w32(bhdr() + 88, sf_part_crc32(btab(), 16384)); reseal_header(bhdr()); BAD("backup table one block longer, checksum made right");
	good(); ptab()[5 * 128 + 60] ^= 0x10; BAD("primary table: one bit flipped (checksum)");
	good(); btab()[5 * 128 + 60] ^= 0x10; BAD("backup table: one bit flipped (checksum)");
	good(); phdr()[40] ^= 1; BAD("primary header: one bit flipped (checksum)");
	good(); bhdr()[40] ^= 1; BAD("backup header: one bit flipped (checksum)");
	good(); memcpy(phdr(), "EFI PARU", 8); reseal_header(phdr()); BAD("primary header: signature");
	good(); w32(bhdr() + 8, 0x00020000); reseal_header(bhdr()); BAD("backup header: revision");
	good(); w64(phdr() + 32, ns - 2); reseal_header(phdr()); BAD("primary header points at another backup");
	good(); w64(bhdr() + 32, 2); reseal_header(bhdr()); BAD("backup header points at another primary");
	good(); w64(bhdr() + 24, ns - 2); reseal_header(bhdr()); BAD("backup header says it lives elsewhere");
	good(); w64(phdr() + 72, 3); reseal_header(phdr()); BAD("primary header: table elsewhere");
	good(); w32(phdr() + 80, 64); reseal_header(phdr()); BAD("primary header: 64 entries");
	good(); w32(phdr() + 84, 256); reseal_header(phdr()); BAD("primary header: entry size 256");
	good(); bhdr()[56] ^= 1; reseal_header(bhdr()); BAD("headers with different disk GUIDs");
	good(); w64(bhdr() + 48, ns - 8); reseal_header(bhdr()); BAD("headers with different usable ranges");
	good(); memcpy(ptab() + 5 * 128 + 16, ptab() + 2 * 128 + 16, 16); memcpy(btab(), ptab(), 16384); seal(); BAD("the GUID occurs twice");
	good(); w64(ptab() + 1 * 128 + 40, kFirst); memcpy(btab(), ptab(), 16384); seal(); BAD("the partition before overlaps its first block");
	good(); w64(ptab() + 3 * 128 + 32, kFirst + kBlocks - 1); memcpy(btab(), ptab(), 16384); seal(); BAD("the partition after overlaps its last block");
	good(); w64(phdr() + 40, kFirst + 1); w64(bhdr() + 40, kFirst + 1); reseal_header(phdr()); reseal_header(bhdr()); BAD("starts before the usable range");
	good(); ns = NS - 1; BAD("the disk is one block shorter than the GPT says");
	good(); ns = NS + 1; BAD("the disk is one block longer than the GPT says");
	for (unsigned k = 1; k <= 4; k++) { char name[48]; good(); fail_read_at = k; snprintf(name, sizeof(name), "read %u fails", k); BAD(name); fail_read_at = 0; }
	good(); memset(head, 0, sizeof(head)); BAD("no primary GPT at all");
	good(); memset(tail, 0, sizeof(tail)); BAD("no backup GPT at all");
	good(); expect("the partition as it is, again", 1, kOurs, kFirst, kBlocks);

	printf("PARTHOST %u cases, %u failures\n", cases, failures);
	printf(failures ? "PARTHOST FAILED\n" : "PARTHOST PASSED\n");
	return failures != 0;
}
