// SPDX-License-Identifier: MIT
/* See sf_nvme_wgate.h. */
#include "sf_nvme_wgate.h"

static uint32_t g32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static int
zero(const uint8_t *sqe, unsigned from, unsigned to)
{
	for (unsigned i = from; i < to; i++)
		if (sqe[i] != 0)
			return 0;
	return 1;
}

int
sf_nvmew_gate(unsigned queue, const uint8_t sqe[64], uint64_t nsBlocks, const struct sf_nvme_fence *fence)
{
	uint8_t opcode = sqe[0];

	/* everything that is not a Write or a Flush on the I/O queue: the read-only gate decides, unchanged */
	if (queue != SF_NVME_Q_IO || (opcode != SF_NVMEW_IO_WRITE && opcode != SF_NVMEW_IO_FLUSH))
		return sf_nvme_gate(queue, sqe, nsBlocks);

	/* flags (fused, SGL), command id is the caller's, reserved words and the metadata pointer */
	if (sqe[1] != 0 || !zero(sqe, 8, 24))
		return SF_NVME_GATE_FIELDS;
	if (g32(sqe + 4) != SF_NVME_NSID)
		return SF_NVME_GATE_RANGE;
	if (fence == 0 || !fence->open || nsBlocks == 0 || fence->first > fence->last || fence->last >= nsBlocks)
		return SF_NVMEW_GATE_FENCE;

	if (opcode == SF_NVMEW_IO_FLUSH)
		/* no data pointers, no command words at all */
		return zero(sqe, 24, 64) ? SF_NVME_GATE_OK : SF_NVME_GATE_FIELDS;

	{
		uint32_t cdw12 = g32(sqe + 48);
		uint64_t slba = (uint64_t)g32(sqe + 40) | ((uint64_t)g32(sqe + 44) << 32);
		uint64_t count = (uint64_t)(cdw12 & 0xffff) + 1;

		/* no FUA, limited retry, protection information, directive; no dataset hints, no reference tags */
		if ((cdw12 >> 16) != 0 || !zero(sqe, 52, 64))
			return SF_NVME_GATE_FIELDS;
		if (count > SF_NVMEW_MAX_WRITE_BLOCKS)
			return SF_NVME_GATE_RANGE;
		/* every block inside [first, last]; written so that nothing can wrap */
		if (slba < fence->first || slba > fence->last || count - 1 > fence->last - slba)
			return SF_NVMEW_GATE_FENCE;
		return SF_NVME_GATE_OK;
	}
}

int
sf_nvmew_inside(unsigned queue, const uint8_t slot[64], const struct sf_nvme_fence *fence)
{
	uint64_t lba = 0, blocks, room;

	if (queue != SF_NVME_Q_IO || slot[0] != SF_NVMEW_IO_WRITE)
		return 1;
	if (fence == 0 || fence->open == 0 || fence->last < fence->first)
		return 0;
	for (int i = 7; i >= 0; i--)
		lba = (lba << 8) | slot[40 + i];
	blocks = (uint64_t)slot[48] + ((uint64_t)slot[49] << 8) + 1;
	if (lba < fence->first)
		return 0;
	room = fence->last - fence->first + 1;          /* cannot wrap: last < the namespace's size */
	if (room == 0)
		return 0;
	return lba - fence->first < room && blocks <= room - (lba - fence->first);
}
