/* SPDX-License-Identifier: MIT */
/* Private, kernel-only finite snapshot. No caller-selected addresses. */
#ifndef SF_PCIEC_DMA_INSPECT_H
#define SF_PCIEC_DMA_INSPECT_H
#include <stdint.h>
#define SF_PCIEC_DMA_INSPECT_VERSION 1u
#define SF_PCIEC_DMA_INSPECT_FUNCTION "SFCIOPCIDMAInspect"
#define SF_PCIEC_DMA_INSPECT_REGS 12u
#define SF_PCIEC_DMA_INSPECT_SAMPLES 4u
struct sf_pciec_dma_sample {
    uint64_t iova, bytes, physical, span, walked, l1e, pte, cpu_par;
    uint64_t words[4]; /* First 32 bytes of retained backing, CPU view only. */
    int32_t walk_error;
    uint32_t slot;
};
struct sf_pciec_dma_inspect {
    uint32_t version, size, mapping_count, table_count;
    uint32_t regs[SF_PCIEC_DMA_INSPECT_REGS];
    uint32_t rid2sid[64], tcr[64]; /* Fixed T8103 USB4 layout, not DART params. */
    uint64_t root_physical, root_cpu_par;
    struct sf_pciec_dma_sample sample[SF_PCIEC_DMA_INSPECT_SAMPLES];
};
#endif
