/* SPDX-License-Identifier: MIT */
/*
 * The driver-side wiring of the WRITE-FENCED build (SFAppleNVMeRW only), in plain C so that
 * the kext and the host test (host-test/wring_host.c) compile this same file:
 *   - the fence may be opened only for the one range this machine's partition has
 *     (compiled in), after the partition was verified in both GPT copies, with the
 *     build's own boot argument; it can be closed, never moved or widened;
 *   - the second block device's block numbers are relative to the fence: nothing above it
 *     can name a block outside;
 *   - what SFAns::ring() does between the caller's command and the doorbell: the gate on
 *     the command as built, the copy into the queue slot, the gate on the bytes in the
 *     slot, the data direction, the second look (sf_nvmew_inside) and only then the TCB.
 *     A refusal leaves the slot zeroed and the TCB untouched.
 */
#ifndef _SF_NVME_WRING_H
#define _SF_NVME_WRING_H

#include "sf_nvme_wgate.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The SiliconForest partition of THIS machine (J293, internal disk): 16,035,840 blocks (61.17 GiB; shrunk twice on 2026-11-12: 2.5 GB for the startup-picker volume, 512 MiB for the boot partition). No boot argument changes these. */
#define SF_FENCE_FIRST  192633862ULL
#define SF_FENCE_BLOCKS 16035840ULL

enum {
	SF_WRING_DIRECTION = 17,        /* the data direction does not fit the opcode and queue */
	SF_WRING_SECOND    = 18,        /* the second look next to the doorbell said no */
	SF_WRING_UNUSABLE  = 19,        /* controller not enabled or dead, or the tag out of range */
};

/* NVMMU TCB (apple_nvmmu_tcb of the Linux driver): 128 bytes */
enum {
	SF_TCB_SIZE = 128, SF_TCB_OPCODE = 0, SF_TCB_DMA_FLAGS = 1, SF_TCB_COMMAND_ID = 2, SF_TCB_LENGTH = 4,
	SF_TCB_PRP1 = 24, SF_TCB_PRP2 = 32,
	SF_TCB_DMA_NONE = 0, SF_TCB_DMA_FROM_DEVICE = 1, SF_TCB_DMA_TO_DEVICE = 2,
};

/*
 * Opens the fence, once. Nonzero (and the fence left closed) unless: it is still closed and
 * was never opened, the partition was verified in both GPT copies (`verified`), the write
 * argument was given (`writeArg`), and first/blocks are exactly the compiled-in range and
 * lie inside the namespace.
 */
int sf_wfence_open(struct sf_nvme_fence *f, uint64_t first, uint64_t blocks, uint64_t nsBlocks, int verified, int writeArg);
/* Closes it for good (sf_wfence_open refuses afterwards). */
void sf_wfence_close(struct sf_nvme_fence *f);

/* Absolute range inside the open fence? 1 or 0. The check made before any write command is built. */
int sf_wabs_ok(const struct sf_nvme_fence *f, uint64_t lba, uint64_t nblks);
/* Block `block` of the fenced device, `nblks` long, as an absolute LBA. 0, or nonzero when any block is outside. */
int sf_wdev_map(const struct sf_nvme_fence *f, uint64_t block, uint64_t nblks, uint64_t *lba);
/* Blocks of the fenced device (0 when the fence was never opened). */
uint64_t sf_wdev_blocks(const struct sf_nvme_fence *f);

/* The TCB's DMA flags for this command, or -1: host-to-device only for opcode 0x01 on the I/O queue. */
int sf_wring_dir(unsigned queue, const uint8_t sqe[64], int hasData);

/*
 * Everything between the command and the doorbell. `usable`: controller enabled and not dead.
 * Returns SF_NVME_GATE_OK with the slot and the TCB filled, or the reason with the slot zeroed
 * (when it had been written) and the TCB untouched.
 */
int sf_wring_prepare(unsigned queue, unsigned tag, unsigned depth, int usable, const uint8_t in[64], int hasData,
    uint64_t nsBlocks, const struct sf_nvme_fence *f, uint8_t slot[64], uint8_t tcb[SF_TCB_SIZE]);

#ifdef SF_WRING_HOST_TEST
/* host test only: called with stage 1 after the copy into the slot, stage 2 after the gate's look at the slot */
extern void (*sf_wring_hook)(int stage, uint8_t *slot);
#endif

#ifdef __cplusplus
}
#endif
#endif
