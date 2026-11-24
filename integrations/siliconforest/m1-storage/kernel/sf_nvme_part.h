/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The one partition of the internal disk that belongs to SiliconForest, identified so that
 * it cannot be confused with any other: by its unique GUID AND its exact first block and
 * length, all three given from outside (boot arguments), and found exactly so in BOTH copies
 * of the GPT, each of which must be valid by itself and agree with the other. Plain C: the
 * kext and the host test compile this same file.
 */
#ifndef SF_NVME_PART_H
#define SF_NVME_PART_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SF_PART_BLOCK 4096u

struct sf_part_want {
	uint8_t  guid[16];      /* the partition's unique GUID as stored in a GPT entry */
	uint64_t first;         /* first block */
	uint64_t blocks;        /* length in blocks */
};

/* Reads `blocks` blocks of SF_PART_BLOCK bytes at `lba` into `out`; 0 on success. */
typedef int (*sf_part_read_t)(void *ctx, uint64_t lba, uint32_t blocks, uint8_t *out);

/* "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" (either case) to the 16 bytes of a GPT entry; 0 on success. */
int sf_part_parse_guid(const char *text, uint8_t out[16]);

/*
 * 0 when the partition is exactly as wanted in both GPT copies of a disk of `ns_blocks`
 * blocks; otherwise a positive number and the reason in `why`. `work` is scratch memory of
 * at least SF_PART_WORK bytes.
 */
#define SF_PART_WORK (10u * SF_PART_BLOCK)
int sf_part_verify(const struct sf_part_want *want, uint64_t ns_blocks, sf_part_read_t rd, void *ctx, uint8_t *work,
    char *why, size_t len);

uint32_t sf_part_crc32(const uint8_t *p, size_t n);

#ifdef __cplusplus
}
#endif
#endif
