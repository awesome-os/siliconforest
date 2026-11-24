// SPDX-License-Identifier: MIT
/* The allowlist of NVMe commands of the read-only driver. See sf_nvme_gate.h. */
#include "sf_nvme_gate.h"

static uint16_t g16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t g32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

/* struct nvme_command by offset */
#define SQE_OPCODE 0
#define SQE_FLAGS  1
#define SQE_NSID   4
#define SQE_CDW2   8
#define SQE_CDW3   12
#define SQE_MPTR   16
#define SQE_CDW10  40
#define SQE_CDW11  44
#define SQE_CDW12  48
#define SQE_CDW13  52
#define SQE_CDW14  56
#define SQE_CDW15  60

int
sf_nvme_opcode_allowed(unsigned queue, uint8_t opcode)
{
	if (queue == SF_NVME_Q_ADMIN) {
		switch (opcode) {
		case SF_NVME_ADMIN_DELETE_SQ:
		case SF_NVME_ADMIN_CREATE_SQ:
		case SF_NVME_ADMIN_GET_LOG_PAGE:
		case SF_NVME_ADMIN_DELETE_CQ:
		case SF_NVME_ADMIN_CREATE_CQ:
		case SF_NVME_ADMIN_IDENTIFY:
			return 1;
		default:
			return 0;
		}
	}
	if (queue == SF_NVME_Q_IO)
		return opcode == SF_NVME_IO_READ;
	return 0;
}

static int
zero(const uint8_t *sqe, unsigned from, unsigned to)
{
	for (unsigned i = from; i < to; i++)
		if (sqe[i] != 0)
			return 0;
	return 1;
}

int
sf_nvme_gate(unsigned queue, const uint8_t sqe[64], uint64_t nsBlocks)
{
	uint8_t opcode = sqe[SQE_OPCODE];
	uint32_t nsid = g32(sqe + SQE_NSID);
	uint32_t cdw10 = g32(sqe + SQE_CDW10), cdw11 = g32(sqe + SQE_CDW11), cdw12 = g32(sqe + SQE_CDW12);

	if (queue != SF_NVME_Q_ADMIN && queue != SF_NVME_Q_IO)
		return SF_NVME_GATE_QUEUE;
	if (!sf_nvme_opcode_allowed(queue, opcode))
		return SF_NVME_GATE_OPCODE;
	/* PRPs only, no fused operation, no metadata pointer, nothing in the reserved words */
	if (sqe[SQE_FLAGS] != 0 || !zero(sqe, SQE_CDW2, SQE_MPTR + 8))
		return SF_NVME_GATE_FIELDS;

	if (queue == SF_NVME_Q_IO) {
		/* Read: namespace 1, no protection information, no FUA/limited retry, no hints */
		uint64_t slba = (uint64_t)cdw10 | ((uint64_t)cdw11 << 32);
		uint64_t count = (uint64_t)(cdw12 & 0xffff) + 1;

		if ((cdw12 >> 16) != 0 || !zero(sqe, SQE_CDW13, 64))
			return SF_NVME_GATE_FIELDS;
		if (nsid != SF_NVME_NSID || nsBlocks == 0 || count > SF_NVME_MAX_READ_BLOCKS ||
		    slba >= nsBlocks || count > nsBlocks - slba)
			return SF_NVME_GATE_RANGE;
		return SF_NVME_GATE_OK;
	}

	switch (opcode) {
	case SF_NVME_ADMIN_IDENTIFY:
		/* CNS 0 (namespace 1) or CNS 1 (controller), controller id 0 */
		if (!((cdw10 == 0 && nsid == SF_NVME_NSID) || (cdw10 == 1 && nsid == 0)) || !zero(sqe, SQE_CDW11, 64))
			return SF_NVME_GATE_FIELDS;
		return SF_NVME_GATE_OK;
	case SF_NVME_ADMIN_GET_LOG_PAGE:
		/* SMART / health, 512 bytes, the controller's log (as Linux' hwmon reads it) */
		if (cdw10 != (SF_NVME_LOG_SMART | (127u << 16)) || nsid != 0xffffffffu || !zero(sqe, SQE_CDW11, 64))
			return SF_NVME_GATE_FIELDS;
		return SF_NVME_GATE_OK;
	case SF_NVME_ADMIN_CREATE_CQ:
	case SF_NVME_ADMIN_CREATE_SQ:
		/* queue 1 only; flags and the CQ binding are the driver's fixed values */
		if (g16(sqe + SQE_CDW10) != SF_NVME_IO_QID || nsid != 0 || !zero(sqe, SQE_CDW12, 64))
			return SF_NVME_GATE_FIELDS;
		if (opcode == SF_NVME_ADMIN_CREATE_CQ && cdw11 != 3u)                 /* contiguous, interrupts on, vector 0 */
			return SF_NVME_GATE_FIELDS;
		if (opcode == SF_NVME_ADMIN_CREATE_SQ && cdw11 != (1u | (1u << 16)))  /* contiguous, CQ 1 */
			return SF_NVME_GATE_FIELDS;
		return SF_NVME_GATE_OK;
	case SF_NVME_ADMIN_DELETE_SQ:
	case SF_NVME_ADMIN_DELETE_CQ:
		if (cdw10 != SF_NVME_IO_QID || nsid != 0 || !zero(sqe, SQE_CDW11, 64))
			return SF_NVME_GATE_FIELDS;
		return SF_NVME_GATE_OK;
	}
	return SF_NVME_GATE_OPCODE;
}
