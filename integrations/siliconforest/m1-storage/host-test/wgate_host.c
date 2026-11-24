// SPDX-License-Identifier: MIT
/*
 * Host test of kernel/sf_nvme_wgate.c, the command gate of the write-fenced build. The fence
 * in most cases is the SiliconForest partition's real range on the M1's disk.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/sf_nvme_wgate.h"

#define NS 244276265ull
#define FIRST 192633862ull
#define LAST 208669701ull
static const struct sf_nvme_fence kFence = { FIRST, LAST, 1 };
static unsigned cases, failures;

static void w32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void mk(uint8_t s[64], uint8_t op, uint32_t nsid, uint64_t lba, uint32_t nlb0)
{
	memset(s, 0, 64); s[0] = op; s[2] = 0x21; w32(s + 4, nsid);
	if (op != SF_NVMEW_IO_FLUSH) { w32(s + 24, 0x12340000); w32(s + 28, 8); w32(s + 40, (uint32_t)lba); w32(s + 44, (uint32_t)(lba >> 32)); w32(s + 48, nlb0); }
}
/* both looks must agree with `want` (1 admitted) */
static void expect(const char *name, int want, unsigned q, const uint8_t s[64], const struct sf_nvme_fence *f)
{
	int g = sf_nvmew_gate(q, s, NS, f) == SF_NVME_GATE_OK, in = sf_nvmew_inside(q, s, f);
	cases++;
	if (g != want || (want && !in) || (!want && q == SF_NVME_Q_IO && s[0] == SF_NVMEW_IO_WRITE && g)) { failures++; printf("WGATEHOST FAIL %s: gate %d inside %d, wanted %d\n", name, g, in, want); }
	/* a write the gate admits must be inside by the second look, and one outside by the second look must not pass the gate */
	if (q == SF_NVME_Q_IO && s[0] == SF_NVMEW_IO_WRITE && g != (g && in)) { failures++; printf("WGATEHOST FAIL %s: the two looks disagree\n", name); }
}
#define W(name, want, lba, n) do { uint8_t s[64]; mk(s, SF_NVMEW_IO_WRITE, 1, (lba), (uint32_t)((n) - 1)); expect(name, want, SF_NVME_Q_IO, s, &kFence); } while (0)

/* the rule, stated again without looking at the gate */
static int rule(unsigned q, const uint8_t s[64], const struct sf_nvme_fence *f)
{
	uint64_t lba = 0; for (int i = 7; i >= 0; i--) lba = lba << 8 | s[40 + i];
	uint32_t nsid = s[4] | s[5] << 8 | s[6] << 16 | (uint32_t)s[7] << 24, c12 = s[48] | s[49] << 8 | s[50] << 16 | (uint32_t)s[51] << 24;
	if (q != SF_NVME_Q_IO || (s[0] != 0 && s[0] != 1)) return sf_nvme_gate(q, s, NS) == SF_NVME_GATE_OK;
	if (s[1]) return 0;
	for (int i = 8; i < 24; i++) if (s[i]) return 0;
	if (nsid != 1 || !f || !f->open || f->first > f->last || f->last >= NS) return 0;
	if (s[0] == 0) { for (int i = 24; i < 64; i++) if (s[i]) return 0; return 1; }
	for (int i = 52; i < 64; i++) if (s[i]) return 0;
	if (c12 >> 16) return 0;
	for (uint64_t k = 0, n = (c12 & 0xffff) + 1; k < n; k++) { if (n > 256) return 0; uint64_t b = lba + k; if (b < lba || b < f->first || b > f->last) return 0; }
	return 1;
}
static uint64_t rs = 0x9e3779b97f4a7c15ull;
static uint64_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }

int main(void)
{
	uint8_t s[64];
	/* the boundaries */
	W("first block", 1, FIRST, 1);
	W("last block", 1, LAST, 1);
	W("one before the first", 0, FIRST - 1, 1);
	W("one after the last", 0, LAST + 1, 1);
	W("256 blocks at the first", 1, FIRST, 256);
	W("256 blocks ending at the last", 1, LAST - 255, 256);
	W("256 blocks ending one past the last", 0, LAST - 254, 256);
	W("2 blocks across the first", 0, FIRST - 1, 2);
	W("2 blocks across the last", 0, LAST, 2);
	W("256 blocks across the first", 0, FIRST - 128, 256);
	W("256 blocks across the last", 0, LAST - 127, 256);
	W("the whole partition minus nothing, in the middle", 1, FIRST + 8388608, 256);
	W("block 0 (protective MBR)", 0, 0, 1);
	W("block 1 (GPT header)", 0, 1, 1);
	W("blocks 2..5 (GPT table)", 0, 2, 4);
	W("first block of the iBoot partition", 0, 6, 1);
	W("first block of macOS", 0, 128006, 1);
	W("last block of macOS (just before ours)", 0, FIRST - 1, 1);
	W("the sliver after ours", 0, 208669702ull, 1);
	W("first block of the Linux stub", 0, 209411119ull, 1);
	W("EFI partition", 0, 210021423ull, 1);
	W("Linux /boot", 0, 210149423ull, 1);
	W("Linux root", 0, 210673711ull, 1);
	W("recovery", 0, 242965551ull, 1);
	W("backup GPT table", 0, NS - 5, 4);
	W("backup GPT header", 0, NS - 1, 1);
	W("past the end of the disk", 0, NS, 1);
	/* wrap-around */
	W("LBA 2^64-1", 0, ~0ull, 1);
	W("LBA 2^64-1, 2 blocks (wraps to 0)", 0, ~0ull, 2);
	W("LBA 2^64-255, 256 blocks", 0, ~0ull - 254, 256);
	W("LBA with our first block in the low word and 1 in the high word", 0, FIRST | (1ull << 32), 1);
	W("LBA 2^32 + small", 0, (1ull << 32) + 5, 1);
	/* length: the field is zero-based (0 = one block); more than 256 is refused */
	mk(s, 1, 1, FIRST, 0); expect("count field 0 (one block) at the first", 1, 1, s, &kFence);
	mk(s, 1, 1, FIRST, 256); expect("257 blocks inside the fence", 0, 1, s, &kFence);
	mk(s, 1, 1, FIRST, 0xffff); expect("65536 blocks inside the fence", 0, 1, s, &kFence);
	/* other namespaces */
	for (uint32_t ns = 0; ns < 5; ns++) if (ns != 1) { mk(s, 1, ns, FIRST, 0); expect("write, another namespace", 0, 1, s, &kFence); mk(s, 0, ns, 0, 0); expect("flush, another namespace", 0, 1, s, &kFence); }
	mk(s, 1, 0xffffffffu, FIRST, 0); expect("write, broadcast namespace", 0, 1, s, &kFence);
	mk(s, 0, 0xffffffffu, 0, 0); expect("flush, broadcast namespace", 0, 1, s, &kFence);
	/* flags and fields */
	mk(s, 1, 1, FIRST, 0); s[51] = 0x40; expect("write with FUA", 0, 1, s, &kFence);
	mk(s, 1, 1, FIRST, 0); s[51] = 0x80; expect("write with limited retry", 0, 1, s, &kFence);
	mk(s, 1, 1, FIRST, 0); s[51] = 0x3c; expect("write with protection information", 0, 1, s, &kFence);
	mk(s, 1, 1, FIRST, 0); s[50] = 0x10; expect("write with a directive type", 0, 1, s, &kFence);
	mk(s, 1, 1, FIRST, 0); s[52] = 1; expect("write with dataset management hints", 0, 1, s, &kFence);
	mk(s, 1, 1, FIRST, 0); s[56] = 1; expect("write with a reference tag", 0, 1, s, &kFence);
	mk(s, 1, 1, FIRST, 0); s[60] = 1; expect("write with application tags", 0, 1, s, &kFence);
	mk(s, 1, 1, FIRST, 0); s[1] = 0x40; expect("write with SGL", 0, 1, s, &kFence);
	mk(s, 1, 1, FIRST, 0); s[1] = 0x01; expect("write, fused", 0, 1, s, &kFence);
	mk(s, 1, 1, FIRST, 0); s[16] = 1; expect("write with a metadata pointer", 0, 1, s, &kFence);
	mk(s, 1, 1, FIRST, 0); s[8] = 1; expect("write with a reserved word set", 0, 1, s, &kFence);
	mk(s, 0, 1, 0, 0); expect("flush", 1, 1, s, &kFence);
	mk(s, 0, 1, 0, 0); s[24] = 1; expect("flush with a data pointer", 0, 1, s, &kFence);
	mk(s, 0, 1, 0, 0); s[40] = 1; expect("flush with a command word", 0, 1, s, &kFence);
	/* the fence itself */
	{ struct sf_nvme_fence f = { FIRST, LAST, 0 }; mk(s, 1, 1, FIRST, 0); expect("fence closed: write", 0, 1, s, &f); mk(s, 0, 1, 0, 0); expect("fence closed: flush", 0, 1, s, &f); }
	mk(s, 1, 1, FIRST, 0); expect("no fence at all: write", 0, 1, s, 0);
	{ struct sf_nvme_fence f = { LAST, FIRST, 1 }; mk(s, 1, 1, FIRST, 0); expect("fence inverted", 0, 1, s, &f); }
	{ struct sf_nvme_fence f = { FIRST, NS, 1 }; mk(s, 1, 1, FIRST, 0); expect("fence reaching past the namespace", 0, 1, s, &f); }
	{ struct sf_nvme_fence f = { 0, ~0ull, 1 }; mk(s, 1, 1, 0, 0); expect("fence = everything", 0, 1, s, &f); }
	{ struct sf_nvme_fence f = { FIRST, FIRST, 1 }; mk(s, 1, 1, FIRST, 0); expect("one-block fence, that block", 1, 1, s, &f); mk(s, 1, 1, FIRST, 1); expect("one-block fence, two blocks", 0, 1, s, &f); }
	/* every opcode on both queues with the fence open: the I/O queue gains exactly 0x00 and 0x01, the admin queue nothing */
	{
		unsigned io = 0, admin_same = 0;
		for (unsigned op = 0; op < 256; op++) {
			mk(s, (uint8_t)op, 1, FIRST, 0); if (op == 0) memset(s + 24, 0, 40);
			if (sf_nvmew_gate(SF_NVME_Q_IO, s, NS, &kFence) == SF_NVME_GATE_OK) { io++; if (op > 2) { failures++; printf("WGATEHOST FAIL I/O opcode 0x%02x admitted\n", op); } }
			for (int shape = 0; shape < 2; shape++) {
				memset(s, 0, 64); s[0] = (uint8_t)op; if (shape) { w32(s + 4, 1); w32(s + 40, (uint32_t)FIRST); }
				admin_same += sf_nvmew_gate(SF_NVME_Q_ADMIN, s, NS, &kFence) == sf_nvme_gate(SF_NVME_Q_ADMIN, s, NS);
			}
			memset(s, 0, 64); s[0] = (uint8_t)op; w32(s + 4, 1);
			if (sf_nvmew_gate(2, s, NS, &kFence) == SF_NVME_GATE_OK) { failures++; printf("WGATEHOST FAIL queue 2 opcode 0x%02x admitted\n", op); }
		}
		cases += 3;
		if (io != 3) { failures++; printf("WGATEHOST FAIL %u I/O opcodes admitted, wanted 3 (flush, write, read)\n", io); }
		if (admin_same != 512) { failures++; printf("WGATEHOST FAIL the admin queue differs from the read-only gate in %u of 512 cases\n", 512 - admin_same); }
		printf("WGATEHOST opcodes: I/O queue admits %u of 256 (0x00 flush, 0x01 write, 0x02 read); admin queue identical to the read-only gate in %u of 512 shapes\n", io, admin_same);
	}
	/* random commands against the rule stated independently; half of them aimed at the fence's edges */
	{
		unsigned long admitted = 0, n = 20000000;
		for (unsigned long i = 0; i < n; i++) {
			unsigned q = rnd() % 8 ? SF_NVME_Q_IO : SF_NVME_Q_ADMIN;
			uint64_t r = rnd(), lba = r % 4 == 0 ? rnd() : r % 4 == 1 ? FIRST - 300 + rnd() % 600 : r % 4 == 2 ? LAST - 300 + rnd() % 600 : FIRST + rnd() % (LAST - FIRST + 1);
			mk(s, (uint8_t)(rnd() % 6 == 0 ? rnd() : rnd() % 3), rnd() % 16 ? 1 : (uint32_t)rnd(), lba, (uint32_t)(rnd() % 8 ? rnd() % 300 : rnd()));
			if (rnd() % 64 == 0) s[rnd() % 64] ^= (uint8_t)(1u << (rnd() % 8));
			if (s[0] == 0 && rnd() % 2) memset(s + 24, 0, 40);
			int g = sf_nvmew_gate(q, s, NS, &kFence) == SF_NVME_GATE_OK, want = rule(q, s, &kFence);
			if (g != want || (g && !sf_nvmew_inside(q, s, &kFence))) { if (failures++ < 10) printf("WGATEHOST FAIL random %lu: gate %d rule %d\n", i, g, want); }
			admitted += (unsigned long)(g && q == SF_NVME_Q_IO && s[0] == 1);
		}
		cases++;
		printf("WGATEHOST random: %lu commands, %lu writes admitted, every one inside the fence by the independent rule and the second look\n", n, admitted);
	}
	printf("WGATEHOST %u named cases, %u failures\n", cases, failures);
	printf(failures ? "WGATEHOST FAILED\n" : "WGATEHOST PASSED\n");
	return failures != 0;
}
