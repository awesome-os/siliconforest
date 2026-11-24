// SPDX-License-Identifier: MIT
/*
 * Host test of the command gate of the read-only NVMe driver (../kernel/sf_nvme_gate.c,
 * the same source the kext links): every opcode on both queues, named destructive
 * commands in realistic shapes, the bounds of a read, and random submission queue
 * entries checked against a second, independently written statement of the rule.
 */
#include "../kernel/sf_nvme_gate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static void w32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t r32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void w64(uint8_t *p, uint64_t v) { w32(p, (uint32_t)v); w32(p + 4, (uint32_t)(v >> 32)); }

static uint64_t rng_state = 0x9e3779b97f4a7c15ULL;
static uint64_t rng(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17; return rng_state; }

/* A realistic I/O command: opcode, namespace 1, a data pointer, an LBA range. */
static void io_cmd(uint8_t *sqe, uint8_t opcode, uint64_t lba, uint32_t blocks)
{
	memset(sqe, 0, 64);
	sqe[0] = opcode;
	w32(sqe + 4, 1);
	w64(sqe + 24, 0x800000000ULL);
	w64(sqe + 32, 0x800001000ULL);
	w64(sqe + 40, lba);
	w32(sqe + 48, blocks - 1);
}

/* The rule again, written from the specification of what the driver may send. */
static int reference(unsigned queue, const uint8_t *s, uint64_t nsBlocks)
{
	uint32_t nsid = r32(s + 4), c10 = r32(s + 40), c11 = r32(s + 44), c12 = r32(s + 48);
	int i;

	if (s[1] != 0)
		return 0;
	for (i = 8; i < 24; i++)
		if (s[i])
			return 0;
	if (queue == SF_NVME_Q_IO) {
		uint64_t lba = c10 | ((uint64_t)c11 << 32), n = (uint64_t)(c12 & 0xffff) + 1;
		if (s[0] != 0x02 || nsid != 1 || (c12 >> 16) != 0 || r32(s + 52) || r32(s + 56) || r32(s + 60))
			return 0;
		return nsBlocks != 0 && n <= 256 && lba < nsBlocks && lba + n <= nsBlocks && lba + n > lba;
	}
	if (queue != SF_NVME_Q_ADMIN)
		return 0;
	switch (s[0]) {
	case 0x06:
		return ((c10 == 0 && nsid == 1) || (c10 == 1 && nsid == 0)) && !c11 && !c12 && !r32(s + 52) && !r32(s + 56) && !r32(s + 60);
	case 0x02:
		return c10 == (2u | (127u << 16)) && nsid == 0xffffffffu && !c11 && !c12 && !r32(s + 52) && !r32(s + 56) && !r32(s + 60);
	case 0x05:
		return (c10 & 0xffff) == 1 && nsid == 0 && c11 == 3 && !c12 && !r32(s + 52) && !r32(s + 56) && !r32(s + 60);
	case 0x01:
		return (c10 & 0xffff) == 1 && nsid == 0 && c11 == 0x10001 && !c12 && !r32(s + 52) && !r32(s + 56) && !r32(s + 60);
	case 0x00:
	case 0x04:
		return c10 == 1 && nsid == 0 && !c11 && !c12 && !r32(s + 52) && !r32(s + 56) && !r32(s + 60);
	}
	return 0;
}

int main(void)
{
	static const struct { uint8_t op; const char *name; } io_forbidden[] = {
		{ 0x00, "Flush" }, { 0x01, "Write" }, { 0x04, "Write Uncorrectable" }, { 0x05, "Compare" },
		{ 0x08, "Write Zeroes" }, { 0x09, "Dataset Management (TRIM)" }, { 0x0c, "Verify" },
		{ 0x0d, "Reservation Register" }, { 0x0e, "Reservation Report" }, { 0x11, "Reservation Acquire" },
		{ 0x15, "Reservation Release" }, { 0x19, "Copy" }, { 0x79, "Zone Management Send" }, { 0x7d, "Zone Append" },
	};
	static const struct { uint8_t op; const char *name; } admin_forbidden[] = {
		{ 0x08, "Abort" }, { 0x09, "Set Features" }, { 0x0a, "Get Features" }, { 0x0c, "Asynchronous Event Request" },
		{ 0x0d, "Namespace Management" }, { 0x10, "Firmware Commit" }, { 0x11, "Firmware Image Download" },
		{ 0x14, "Device Self-test" }, { 0x15, "Namespace Attachment" }, { 0x18, "Keep Alive" },
		{ 0x19, "Directive Send" }, { 0x1c, "Virtualization Management" }, { 0x1d, "NVMe-MI Send" },
		{ 0x7c, "Doorbell Buffer Config" }, { 0x80, "Format NVM" }, { 0x81, "Security Send" },
		{ 0x82, "Security Receive" }, { 0x84, "Sanitize" }, { 0x86, "Get LBA Status" }, { 0xd8, "Apple vendor" },
	};
	const uint64_t ns = 61035008;
	uint8_t sqe[64];
	unsigned allowed = 0, q, op, i;

	/* 1. every opcode, both queues: the allowlist is exactly seven pairs */
	for (q = 0; q < 2; q++)
		for (op = 0; op < 256; op++)
			if (sf_nvme_opcode_allowed(q, (uint8_t)op)) {
				allowed++;
				CHECK((q == SF_NVME_Q_ADMIN && (op == 0x00 || op == 0x01 || op == 0x02 || op == 0x04 || op == 0x05 || op == 0x06)) ||
				    (q == SF_NVME_Q_IO && op == 0x02), "queue %u opcode 0x%02x is allowed", q, op);
			}
	CHECK(allowed == 7, "%u queue/opcode pairs are allowed, expected 7", allowed);
	for (q = 2; q < 6; q++)
		for (op = 0; op < 256; op++)
			CHECK(!sf_nvme_opcode_allowed(q, (uint8_t)op), "unknown queue %u admits opcode 0x%02x", q, op);

	/* 2. named commands that change the medium or the controller, in realistic shapes */
	for (i = 0; i < sizeof(io_forbidden) / sizeof(io_forbidden[0]); i++) {
		io_cmd(sqe, io_forbidden[i].op, 1000, 8);
		CHECK(sf_nvme_gate(SF_NVME_Q_IO, sqe, ns) == SF_NVME_GATE_OPCODE, "I/O %s (0x%02x) passes the gate", io_forbidden[i].name,
		    io_forbidden[i].op);
	}
	for (i = 0; i < sizeof(admin_forbidden) / sizeof(admin_forbidden[0]); i++) {
		memset(sqe, 0, 64);
		sqe[0] = admin_forbidden[i].op;
		CHECK(sf_nvme_gate(SF_NVME_Q_ADMIN, sqe, ns) == SF_NVME_GATE_OPCODE, "admin %s (0x%02x) passes the gate (bare)",
		    admin_forbidden[i].name, admin_forbidden[i].op);
		w32(sqe + 4, 1);
		w64(sqe + 24, 0x800000000ULL);
		w32(sqe + 40, 1);
		CHECK(sf_nvme_gate(SF_NVME_Q_ADMIN, sqe, ns) == SF_NVME_GATE_OPCODE, "admin %s (0x%02x) passes the gate",
		    admin_forbidden[i].name, admin_forbidden[i].op);
	}
	/* the admin opcodes whose numbers are I/O Flush and Write must not carry an I/O-shaped command */
	io_cmd(sqe, 0x01, 1000, 8);
	CHECK(sf_nvme_gate(SF_NVME_Q_ADMIN, sqe, ns) != SF_NVME_GATE_OK, "a Write-shaped command passes as admin Create SQ");
	io_cmd(sqe, 0x00, 0, 1);
	CHECK(sf_nvme_gate(SF_NVME_Q_ADMIN, sqe, ns) != SF_NVME_GATE_OK, "a Flush-shaped command passes as admin Delete SQ");
	/* other log pages and Identify forms are not read */
	memset(sqe, 0, 64); sqe[0] = 0x02; w32(sqe + 4, 0xffffffffu); w32(sqe + 40, 0x03 | (127u << 16));
	CHECK(sf_nvme_gate(SF_NVME_Q_ADMIN, sqe, ns) == SF_NVME_GATE_FIELDS, "Get Log Page 3 passes");
	memset(sqe, 0, 64); sqe[0] = 0x06; w32(sqe + 4, 2); w32(sqe + 40, 0);
	CHECK(sf_nvme_gate(SF_NVME_Q_ADMIN, sqe, ns) == SF_NVME_GATE_FIELDS, "Identify Namespace 2 passes");
	memset(sqe, 0, 64); sqe[0] = 0x05; w32(sqe + 40, 2 | (63u << 16)); w32(sqe + 44, 3);
	CHECK(sf_nvme_gate(SF_NVME_Q_ADMIN, sqe, ns) == SF_NVME_GATE_FIELDS, "Create CQ 2 passes");

	/* 3. the commands the driver sends pass */
	memset(sqe, 0, 64); sqe[0] = 0x06; w32(sqe + 40, 1); w64(sqe + 24, 0x800000000ULL);
	CHECK(sf_nvme_gate(SF_NVME_Q_ADMIN, sqe, 0) == SF_NVME_GATE_OK, "Identify Controller refused");
	memset(sqe, 0, 64); sqe[0] = 0x06; w32(sqe + 4, 1); w64(sqe + 24, 0x800000000ULL);
	CHECK(sf_nvme_gate(SF_NVME_Q_ADMIN, sqe, 0) == SF_NVME_GATE_OK, "Identify Namespace 1 refused");
	memset(sqe, 0, 64); sqe[0] = 0x02; w32(sqe + 4, 0xffffffffu); w32(sqe + 40, 0x02 | (127u << 16)); w64(sqe + 24, 0x800000000ULL);
	CHECK(sf_nvme_gate(SF_NVME_Q_ADMIN, sqe, 0) == SF_NVME_GATE_OK, "SMART log refused");
	memset(sqe, 0, 64); sqe[0] = 0x05; w32(sqe + 40, 1 | (63u << 16)); w32(sqe + 44, 3); w64(sqe + 24, 0x800000000ULL);
	CHECK(sf_nvme_gate(SF_NVME_Q_ADMIN, sqe, 0) == SF_NVME_GATE_OK, "Create CQ refused");
	memset(sqe, 0, 64); sqe[0] = 0x01; w32(sqe + 40, 1 | (63u << 16)); w32(sqe + 44, 0x10001); w64(sqe + 24, 0x800000000ULL);
	CHECK(sf_nvme_gate(SF_NVME_Q_ADMIN, sqe, 0) == SF_NVME_GATE_OK, "Create SQ refused");
	memset(sqe, 0, 64); sqe[0] = 0x00; w32(sqe + 40, 1);
	CHECK(sf_nvme_gate(SF_NVME_Q_ADMIN, sqe, 0) == SF_NVME_GATE_OK, "Delete SQ refused");
	memset(sqe, 0, 64); sqe[0] = 0x04; w32(sqe + 40, 1);
	CHECK(sf_nvme_gate(SF_NVME_Q_ADMIN, sqe, 0) == SF_NVME_GATE_OK, "Delete CQ refused");

	/* 4. reads: inside namespace 1 only */
	io_cmd(sqe, 0x02, 0, 1);            CHECK(sf_nvme_gate(SF_NVME_Q_IO, sqe, ns) == SF_NVME_GATE_OK, "read of block 0 refused");
	io_cmd(sqe, 0x02, ns - 256, 256);   CHECK(sf_nvme_gate(SF_NVME_Q_IO, sqe, ns) == SF_NVME_GATE_OK, "read of the last 256 blocks refused");
	io_cmd(sqe, 0x02, ns - 1, 1);       CHECK(sf_nvme_gate(SF_NVME_Q_IO, sqe, ns) == SF_NVME_GATE_OK, "read of the last block refused");
	io_cmd(sqe, 0x02, ns, 1);           CHECK(sf_nvme_gate(SF_NVME_Q_IO, sqe, ns) == SF_NVME_GATE_RANGE, "read past the end passes");
	io_cmd(sqe, 0x02, ns - 1, 2);       CHECK(sf_nvme_gate(SF_NVME_Q_IO, sqe, ns) == SF_NVME_GATE_RANGE, "read across the end passes");
	io_cmd(sqe, 0x02, 0, 257);          CHECK(sf_nvme_gate(SF_NVME_Q_IO, sqe, ns) == SF_NVME_GATE_RANGE, "read of 257 blocks passes");
	io_cmd(sqe, 0x02, ~0ULL, 2);        CHECK(sf_nvme_gate(SF_NVME_Q_IO, sqe, ns) == SF_NVME_GATE_RANGE, "read wrapping the LBA passes");
	io_cmd(sqe, 0x02, 0, 1);            CHECK(sf_nvme_gate(SF_NVME_Q_IO, sqe, 0) == SF_NVME_GATE_RANGE, "read before the namespace is known passes");
	io_cmd(sqe, 0x02, 0, 1); w32(sqe + 4, 2);
	CHECK(sf_nvme_gate(SF_NVME_Q_IO, sqe, ns) == SF_NVME_GATE_RANGE, "read of namespace 2 passes");
	io_cmd(sqe, 0x02, 0, 1); w32(sqe + 4, 0xffffffffu);
	CHECK(sf_nvme_gate(SF_NVME_Q_IO, sqe, ns) == SF_NVME_GATE_RANGE, "read of the broadcast namespace passes");
	io_cmd(sqe, 0x02, 0, 1); w32(sqe + 48, 1u << 30);
	CHECK(sf_nvme_gate(SF_NVME_Q_IO, sqe, ns) == SF_NVME_GATE_FIELDS, "read with FUA passes");
	io_cmd(sqe, 0x02, 0, 1); sqe[1] = 0x40;
	CHECK(sf_nvme_gate(SF_NVME_Q_IO, sqe, ns) == SF_NVME_GATE_FIELDS, "read with SGL passes");
	io_cmd(sqe, 0x02, 0, 1);
	CHECK(sf_nvme_gate(SF_NVME_Q_ADMIN, sqe, ns) != SF_NVME_GATE_OK, "a read-shaped command passes as admin Get Log Page");

	/* 5. random entries: the gate agrees with the reference, and nothing but the allowlist ever passes */
	{
		unsigned long passed = 0, n;
		for (n = 0; n < 20000000UL; n++) {
			unsigned queue = (unsigned)(rng() % 3);
			uint64_t blocks = (rng() & 1) ? ns : (rng() % 5000);
			int got, want;

			for (i = 0; i < 64; i += 8) {
				uint64_t v = rng();
				memcpy(sqe + i, &v, 8);
			}
			/* steer a share of the entries toward the allowed shapes so that the accept paths are exercised */
			if (rng() % 4) {
				static const uint8_t ops[] = { 0, 1, 2, 4, 5, 6, 8, 9, 0x80 };
				sqe[0] = ops[rng() % sizeof(ops)];
				sqe[1] = 0;
				memset(sqe + 8, 0, 16);
				if (rng() % 2) memset(sqe + 52, 0, 12);
				if (rng() % 2) w32(sqe + 4, (uint32_t)(rng() % 3));
				if (rng() % 2) w32(sqe + 44, (uint32_t)(rng() % 4));
				if (rng() % 2) w32(sqe + 48, (uint32_t)(rng() % 300));
				if (rng() % 2) w32(sqe + 40, (uint32_t)(rng() % 3));
				if (rng() % 8 == 0) { w32(sqe + 40, 1 | (63u << 16)); w32(sqe + 44, (rng() & 1) ? 3 : 0x10001); w32(sqe + 48, 0); w32(sqe + 4, 0); }
				if (rng() % 8 == 0) { w32(sqe + 40, 2 | (127u << 16)); w32(sqe + 44, 0); w32(sqe + 48, 0); w32(sqe + 4, 0xffffffffu); }
			}
			got = sf_nvme_gate(queue, sqe, blocks) == SF_NVME_GATE_OK;
			want = reference(queue, sqe, blocks);
			if (got != want) {
				CHECK(0, "gate %d, reference %d for queue %u opcode 0x%02x", got, want, queue, sqe[0]);
				break;
			}
			if (got) {
				passed++;
				CHECK(sf_nvme_opcode_allowed(queue, sqe[0]), "an entry with opcode 0x%02x passed on queue %u", sqe[0], queue);
			}
		}
		printf("GATEHOST random: %lu entries, %lu passed, all of them on the allowlist\n", n, passed);
		CHECK(passed > 1000, "the random test exercised only %lu accepted entries", passed);
	}
	printf("GATEHOST %s (%u failures)\n", failures ? "FAILED" : "PASSED", failures);
	return failures ? 1 : 0;
}
