// SPDX-License-Identifier: MIT
/*
 * Host test of kernel-rw/sf_nvme_wring.c: the driver-side wiring of the write-fenced build
 * (fence constants, the second device's block arithmetic, the data direction, and the
 * sequence between a command and the doorbell including a slot that changes under it).
 */
#include <stdio.h>
#include <string.h>
#define SF_WRING_HOST_TEST 1
#include "../kernel-rw/sf_nvme_wring.h"

#define NS 244276265ull
#define FIRST 192633862ull
#define BLOCKS 16035840ull
#define LAST (FIRST + BLOCKS - 1)
static unsigned cases, failures;
#define CHECK(name, cond) do { cases++; if (!(cond)) { failures++; printf("WRINGHOST FAIL %s\n", name); } } while (0)

static void w32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void mk(uint8_t s[64], uint8_t op, uint64_t lba, uint32_t n)
{
	memset(s, 0, 64); s[0] = op; w32(s + 4, 1);
	if (op != SF_NVMEW_IO_FLUSH) { w32(s + 24, 0x12340000); w32(s + 28, 8); w32(s + 40, (uint32_t)lba); w32(s + 44, (uint32_t)(lba >> 32)); w32(s + 48, n - 1); }
}
static int zeroed(const uint8_t *p, unsigned n) { for (unsigned i = 0; i < n; i++) if (p[i]) return 0; return 1; }

/* the tamper hook */
static int hStage; static uint64_t hLba; static int hOp = -1, hCalls;
static void hook(int stage, uint8_t *slot)
{
	hCalls++;
	if (stage != hStage) return;
	if (hOp >= 0) slot[0] = (uint8_t)hOp;
	else { w32(slot + 40, (uint32_t)hLba); w32(slot + 44, (uint32_t)(hLba >> 32)); }
}

static int prep(unsigned q, unsigned tag, int usable, const uint8_t in[64], int hasData, const struct sf_nvme_fence *f, uint8_t slot[64], uint8_t tcb[128])
{
	memset(slot, 0xa5, 64); memset(tcb, 0x5a, 128);
	return sf_wring_prepare(q, tag, 64, usable, in, hasData, NS, f, slot, tcb);
}
static int tcb_untouched(const uint8_t *tcb) { for (int i = 0; i < 128; i++) if (tcb[i] != 0x5a) return 0; return 1; }

int main(void)
{
	struct sf_nvme_fence f = { 0, 0, 0 }, open = { FIRST, LAST, 1 };
	uint8_t s[64], slot[64], tcb[128];
	uint64_t lba = 0;

	/* ---- the fence: only this machine's range, only verified, only with the argument, only once ---- */
	CHECK("constants", SF_FENCE_FIRST == 192633862ull && SF_FENCE_BLOCKS == 16035840ull && LAST == 208669701ull);
	CHECK("not verified", sf_wfence_open(&f, FIRST, BLOCKS, NS, 0, 1) != 0 && !f.open && !f.first && !f.last);
	CHECK("verified = 2", sf_wfence_open(&f, FIRST, BLOCKS, NS, 2, 1) != 0 && !f.open);
	CHECK("no write argument", sf_wfence_open(&f, FIRST, BLOCKS, NS, 1, 0) != 0 && !f.open);
	CHECK("write argument 2", sf_wfence_open(&f, FIRST, BLOCKS, NS, 1, 2) != 0 && !f.open);
	CHECK("first - 1", sf_wfence_open(&f, FIRST - 1, BLOCKS, NS, 1, 1) != 0 && !f.open);
	CHECK("first + 1", sf_wfence_open(&f, FIRST + 1, BLOCKS, NS, 1, 1) != 0 && !f.open);
	CHECK("blocks + 1", sf_wfence_open(&f, FIRST, BLOCKS + 1, NS, 1, 1) != 0 && !f.open);
	CHECK("blocks - 1", sf_wfence_open(&f, FIRST, BLOCKS - 1, NS, 1, 1) != 0 && !f.open);
	CHECK("whole disk", sf_wfence_open(&f, 0, NS, NS, 1, 1) != 0 && !f.open);
	CHECK("another partition", sf_wfence_open(&f, 76806, 500000, NS, 1, 1) != 0 && !f.open);
	CHECK("namespace too small", sf_wfence_open(&f, FIRST, BLOCKS, LAST, 1, 1) != 0 && !f.open);
	CHECK("namespace unknown", sf_wfence_open(&f, FIRST, BLOCKS, 0, 1, 1) != 0 && !f.open);
	CHECK("no fence", sf_wfence_open(0, FIRST, BLOCKS, NS, 1, 1) != 0);
	CHECK("closed: no absolute range", !sf_wabs_ok(&f, FIRST, 1) && sf_wdev_blocks(&f) == 0 && sf_wdev_map(&f, 0, 1, &lba) != 0);
	CHECK("opens", sf_wfence_open(&f, FIRST, BLOCKS, NS, 1, 1) == 0 && f.open == 1 && f.first == FIRST && f.last == LAST);
	CHECK("namespace exactly large enough", ({ struct sf_nvme_fence g = { 0, 0, 0 }; sf_wfence_open(&g, FIRST, BLOCKS, LAST + 1, 1, 1) == 0; }));
	CHECK("not twice", sf_wfence_open(&f, FIRST, BLOCKS, NS, 1, 1) != 0 && f.open == 1 && f.first == FIRST && f.last == LAST);
	sf_wfence_close(&f);
	CHECK("closed", f.open == 0 && f.first == FIRST && f.last == LAST && !sf_wabs_ok(&f, FIRST, 1));
	CHECK("never again", sf_wfence_open(&f, FIRST, BLOCKS, NS, 1, 1) != 0 && f.open == 0);
	CHECK("closed: the device keeps its size", sf_wdev_blocks(&f) == BLOCKS);
	mk(s, SF_NVMEW_IO_WRITE, FIRST, 1);
	CHECK("closed: a write is refused, slot zeroed", prep(SF_NVME_Q_IO, 2, 1, s, 1, &f, slot, tcb) == SF_NVMEW_GATE_FENCE && tcb_untouched(tcb));
	mk(s, SF_NVMEW_IO_FLUSH, 0, 0);
	CHECK("closed: a flush is refused", prep(SF_NVME_Q_IO, 2, 1, s, 0, &f, slot, tcb) == SF_NVMEW_GATE_FENCE && tcb_untouched(tcb));
	mk(s, SF_NVME_IO_READ, 5, 1);
	CHECK("closed: a read goes", prep(SF_NVME_Q_IO, 2, 1, s, 1, &f, slot, tcb) == SF_NVME_GATE_OK && tcb[SF_TCB_DMA_FLAGS] == SF_TCB_DMA_FROM_DEVICE);

	/* ---- absolute ranges: the check before a command is built ---- */
	CHECK("abs first", sf_wabs_ok(&open, FIRST, 1));
	CHECK("abs last", sf_wabs_ok(&open, LAST, 1));
	CHECK("abs whole", sf_wabs_ok(&open, FIRST, BLOCKS));
	CHECK("abs first - 1", !sf_wabs_ok(&open, FIRST - 1, 1));
	CHECK("abs first - 1, two blocks", !sf_wabs_ok(&open, FIRST - 1, 2));
	CHECK("abs last + 1", !sf_wabs_ok(&open, LAST + 1, 1));
	CHECK("abs last, two blocks", !sf_wabs_ok(&open, LAST, 2));
	CHECK("abs whole + 1", !sf_wabs_ok(&open, FIRST, BLOCKS + 1));
	CHECK("abs zero blocks", !sf_wabs_ok(&open, FIRST, 0));
	CHECK("abs block 0", !sf_wabs_ok(&open, 0, 1));
	CHECK("abs GPT header", !sf_wabs_ok(&open, 1, 1));
	CHECK("abs backup GPT", !sf_wabs_ok(&open, NS - 1, 1));
	CHECK("abs wrap", !sf_wabs_ok(&open, ~0ull, 2) && !sf_wabs_ok(&open, FIRST, ~0ull) && !sf_wabs_ok(&open, ~0ull - 5, FIRST + 10));
	CHECK("abs high word", !sf_wabs_ok(&open, FIRST | (1ull << 32), 1) && !sf_wabs_ok(&open, FIRST | (1ull << 63), 1));
	CHECK("abs no fence", !sf_wabs_ok(0, FIRST, 1));
	{ struct sf_nvme_fence inv = { LAST, FIRST, 1 }; CHECK("abs inverted fence", !sf_wabs_ok(&inv, FIRST, 1) && sf_wdev_blocks(&inv) == 0); }

	/* ---- the second device: block numbers relative to the fence ---- */
	CHECK("dev size", sf_wdev_blocks(&open) == BLOCKS);
	CHECK("dev block 0", sf_wdev_map(&open, 0, 1, &lba) == 0 && lba == FIRST);
	CHECK("dev last block", sf_wdev_map(&open, BLOCKS - 1, 1, &lba) == 0 && lba == LAST);
	CHECK("dev whole", sf_wdev_map(&open, 0, BLOCKS, &lba) == 0 && lba == FIRST);
	lba = 77;
	CHECK("dev last + 1", sf_wdev_map(&open, BLOCKS, 1, &lba) != 0 && lba == 77);
	CHECK("dev last, two blocks", sf_wdev_map(&open, BLOCKS - 1, 2, &lba) != 0 && lba == 77);
	CHECK("dev zero blocks", sf_wdev_map(&open, 0, 0, &lba) != 0);
	CHECK("dev 2^64 - 1", sf_wdev_map(&open, ~0ull, 1, &lba) != 0 && sf_wdev_map(&open, ~0ull, 2, &lba) != 0);
	CHECK("dev wrap to the start", sf_wdev_map(&open, 0 - FIRST, 1, &lba) != 0);      /* first + block would be 0 */
	CHECK("dev wrap of the count", sf_wdev_map(&open, 1, ~0ull, &lba) != 0 && lba == 77);
	CHECK("dev no result pointer", sf_wdev_map(&open, 0, 1, 0) != 0);
	for (uint64_t b = 0; b < BLOCKS; b += 65521) {
		uint64_t n = (b % 256) + 1, a = 0;
		int r = sf_wdev_map(&open, b, n, &a), want = b + n <= BLOCKS;
		if ((r == 0) != want || (r == 0 && (a != FIRST + b || !sf_wabs_ok(&open, a, n)))) { failures++; printf("WRINGHOST FAIL dev sweep at %llu\n", (unsigned long long)b); }
	}
	cases++;

	/* ---- data direction ---- */
	mk(s, SF_NVMEW_IO_WRITE, FIRST, 1);
	CHECK("dir write", sf_wring_dir(SF_NVME_Q_IO, s, 1) == SF_TCB_DMA_TO_DEVICE);
	CHECK("dir write without data", sf_wring_dir(SF_NVME_Q_IO, s, 0) == -1);
	memset(s + 24, 0, 8);
	CHECK("dir write without pointer", sf_wring_dir(SF_NVME_Q_IO, s, 1) == -1);
	mk(s, SF_NVME_IO_READ, FIRST, 1);
	CHECK("dir read", sf_wring_dir(SF_NVME_Q_IO, s, 1) == SF_TCB_DMA_FROM_DEVICE);
	CHECK("dir read without data", sf_wring_dir(SF_NVME_Q_IO, s, 0) == -1);
	mk(s, SF_NVMEW_IO_FLUSH, 0, 0);
	CHECK("dir flush", sf_wring_dir(SF_NVME_Q_IO, s, 0) == SF_TCB_DMA_NONE);
	CHECK("dir flush with data", sf_wring_dir(SF_NVME_Q_IO, s, 1) == -1);
	s[24] = 1;
	CHECK("dir flush with pointer", sf_wring_dir(SF_NVME_Q_IO, s, 0) == -1);
	{
		unsigned to = 0, from = 0, none = 0, refused = 0;
		for (unsigned q = 0; q < 3; q++) for (unsigned op = 0; op < 256; op++) for (int data = 0; data < 2; data++) for (int ptr = 0; ptr < 2; ptr++) {
			memset(s, 0, 64); s[0] = (uint8_t)op; if (ptr) s[25] = 0x40;
			int d = sf_wring_dir(q, s, data);
			if (d == SF_TCB_DMA_TO_DEVICE) {
				to++;
				/* host-to-device: opcode 0x01 on the I/O queue, or the two queue creations on the admin queue */
				if (!((q == SF_NVME_Q_IO && op == 0x01) || (q == SF_NVME_Q_ADMIN && (op == 0x01 || op == 0x05))) || !data || !ptr) { failures++; printf("WRINGHOST FAIL host-to-device for queue %u opcode 0x%02x\n", q, op); }
			} else if (d == SF_TCB_DMA_FROM_DEVICE) from++; else if (d == SF_TCB_DMA_NONE) none++; else refused++;
		}
		CHECK("dir sweep: host-to-device exactly 3 times", to == 3);
		CHECK("dir sweep: nothing on a third queue", from + none + to + refused == 3 * 256 * 4);
	}

	/* ---- command to doorbell ---- */
	mk(s, SF_NVMEW_IO_WRITE, FIRST, 256); s[2] = 0x77; s[3] = 0x77;
	CHECK("write admitted", prep(SF_NVME_Q_IO, 9, 1, s, 1, &open, slot, tcb) == SF_NVME_GATE_OK);
	CHECK("slot: the tag, the command", slot[0] == 1 && slot[2] == 9 && slot[3] == 0 && memcmp(slot + 4, s + 4, 60) == 0);
	CHECK("tcb: host-to-device, tag, length, pointers", tcb[SF_TCB_OPCODE] == 0 && tcb[SF_TCB_DMA_FLAGS] == SF_TCB_DMA_TO_DEVICE &&
	    tcb[SF_TCB_COMMAND_ID] == 9 && tcb[SF_TCB_LENGTH] == 255 && tcb[SF_TCB_LENGTH + 1] == 0 && memcmp(tcb + SF_TCB_PRP1, s + 24, 16) == 0);
	mk(s, SF_NVME_IO_READ, 0, 1);
	CHECK("read admitted, device-to-host", prep(SF_NVME_Q_IO, 3, 1, s, 1, &open, slot, tcb) == SF_NVME_GATE_OK && tcb[SF_TCB_DMA_FLAGS] == SF_TCB_DMA_FROM_DEVICE);
	mk(s, SF_NVMEW_IO_FLUSH, 0, 0);
	CHECK("flush admitted, no DMA", prep(SF_NVME_Q_IO, 3, 1, s, 0, &open, slot, tcb) == SF_NVME_GATE_OK && tcb[SF_TCB_DMA_FLAGS] == 0 && zeroed(tcb + SF_TCB_PRP1, 16));
	mk(s, SF_NVMEW_IO_WRITE, FIRST - 1, 1);
	CHECK("write one before: refused before the slot is written", prep(SF_NVME_Q_IO, 3, 1, s, 1, &open, slot, tcb) == SF_NVMEW_GATE_FENCE && slot[0] == 0xa5 && tcb_untouched(tcb));
	mk(s, SF_NVMEW_IO_WRITE, LAST, 2);
	CHECK("write across the end: refused", prep(SF_NVME_Q_IO, 3, 1, s, 1, &open, slot, tcb) == SF_NVMEW_GATE_FENCE && tcb_untouched(tcb));
	mk(s, SF_NVMEW_IO_WRITE, FIRST, 1);
	CHECK("write, controller not usable", prep(SF_NVME_Q_IO, 3, 0, s, 1, &open, slot, tcb) == SF_WRING_UNUSABLE && slot[0] == 0xa5 && tcb_untouched(tcb));
	CHECK("write, tag out of range", prep(SF_NVME_Q_IO, 64, 1, s, 1, &open, slot, tcb) == SF_WRING_UNUSABLE && tcb_untouched(tcb));
	CHECK("write on the admin queue is Create SQ: fields refused", prep(SF_NVME_Q_ADMIN, 0, 1, s, 1, &open, slot, tcb) != SF_NVME_GATE_OK && tcb_untouched(tcb));
	CHECK("write on a third queue", prep(2, 3, 1, s, 1, &open, slot, tcb) != SF_NVME_GATE_OK && tcb_untouched(tcb));
	CHECK("write flagged as no data", prep(SF_NVME_Q_IO, 3, 1, s, 0, &open, slot, tcb) == SF_WRING_DIRECTION && zeroed(slot, 64) && tcb_untouched(tcb));
	for (unsigned op = 0; op < 256; op++) {
		mk(s, SF_NVME_IO_READ, FIRST, 1); s[0] = (uint8_t)op; if (op == 0) memset(s + 24, 0, 40);
		int r = prep(SF_NVME_Q_IO, 3, 1, s, op != 0, &open, slot, tcb);
		if ((r == SF_NVME_GATE_OK) != (op <= 2)) { failures++; printf("WRINGHOST FAIL opcode 0x%02x on the I/O queue: %d\n", op, r); }
	}
	cases++;

	/* the slot changes under the driver */
	sf_wring_hook = hook;
	mk(s, SF_NVMEW_IO_WRITE, FIRST, 1);
	static const uint64_t kBad[] = { FIRST - 1, LAST + 1, 0, 1, 6, 76806, NS - 1, ~0ull, FIRST | (1ull << 32) };
	for (unsigned i = 0; i < sizeof(kBad) / sizeof(kBad[0]); i++) {
		hOp = -1; hLba = kBad[i];
		hStage = 1; hCalls = 0;
		CHECK("slot moved outside after the copy: the gate's second call refuses, slot zeroed", prep(SF_NVME_Q_IO, 4, 1, s, 1, &open, slot, tcb) == SF_NVMEW_GATE_FENCE && zeroed(slot, 64) && tcb_untouched(tcb) && hCalls == 2);
		hStage = 2;
		CHECK("slot moved outside after both gate calls: the second look refuses, slot zeroed", prep(SF_NVME_Q_IO, 4, 1, s, 1, &open, slot, tcb) == SF_WRING_SECOND && zeroed(slot, 64) && tcb_untouched(tcb));
	}
	mk(s, SF_NVME_IO_READ, 0, 1);      /* a read of block 0 that turns into a write of block 0 */
	hOp = SF_NVMEW_IO_WRITE; hStage = 1;
	CHECK("read turned write after the copy", prep(SF_NVME_Q_IO, 4, 1, s, 1, &open, slot, tcb) == SF_NVMEW_GATE_FENCE && zeroed(slot, 64) && tcb_untouched(tcb));
	hStage = 2;
	CHECK("read turned write after both gate calls", prep(SF_NVME_Q_IO, 4, 1, s, 1, &open, slot, tcb) == SF_WRING_SECOND && zeroed(slot, 64) && tcb_untouched(tcb));
	for (unsigned op = 3; op < 256; op++) {
		hOp = (int)op; hStage = 2;
		if (prep(SF_NVME_Q_IO, 4, 1, s, 1, &open, slot, tcb) != SF_WRING_DIRECTION || !zeroed(slot, 64) || !tcb_untouched(tcb)) { failures++; printf("WRINGHOST FAIL slot turned opcode 0x%02x after both gate calls\n", op); }
	}
	cases++;
	hOp = SF_NVMEW_IO_FLUSH; hStage = 2;   /* a flush with a data pointer */
	CHECK("read turned flush after both gate calls", prep(SF_NVME_Q_IO, 4, 1, s, 1, &open, slot, tcb) == SF_WRING_DIRECTION && zeroed(slot, 64) && tcb_untouched(tcb));
	sf_wring_hook = 0;

	/* random: whatever prepare admits as a write is inside, by the independent statement */
	{
		uint64_t rs = 0x2545f4914f6cdd1dull, admitted = 0;
		for (unsigned i = 0; i < 4000000; i++) {
			rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
			uint64_t edge = (rs & 1) ? FIRST : LAST, l = edge + (int64_t)((rs >> 8) % 600) - 300;
			uint32_t n = (uint32_t)((rs >> 24) % 300) + 1;
			mk(s, SF_NVMEW_IO_WRITE, l, n);
			int r = sf_wring_prepare(SF_NVME_Q_IO, 5, 64, 1, s, 1, NS, &open, slot, tcb), want = n <= 256 && l >= FIRST && l + n - 1 <= LAST;
			if ((r == SF_NVME_GATE_OK) != want || (want != sf_wabs_ok(&open, l, n) && n <= 256)) { failures++; if (failures < 5) printf("WRINGHOST FAIL random lba %llu n %u\n", (unsigned long long)l, n); }
			admitted += r == SF_NVME_GATE_OK;
		}
		cases++;
		printf("WRINGHOST random: 4000000 writes at the edges, %llu admitted\n", (unsigned long long)admitted);
	}
	printf("WRINGHOST %s: %u cases, %u failures\n", failures ? "FAILED" : "PASSED", cases, failures);
	return failures != 0;
}
