/* Original software codec for T8103 UAT descriptors. License pending.
 * No table walk, allocation, DMA, or MMIO. UAT_ERR_* leaves outputs unchanged.
 * UAT_REFUSED publishes block facts only; it preserves the separate PA output.
 * Callers supply valid, sufficiently sized buffers with appropriate lifetimes.
 */
#ifndef UAT_T8103_H
#define UAT_T8103_H
#include <stdint.h>

#define UAT_OK 0
#define UAT_ERR_FORMAT 1
#define UAT_ERR_ARGUMENT 2
#define UAT_ERR_ADDRESS 3
#define UAT_ERR_ALIGNMENT 4
#define UAT_ERR_CANONICAL 5
#define UAT_ERR_RESERVED 6
#define UAT_ERR_DESCRIPTOR 7
#define UAT_ERR_LEVEL 8
#define UAT_ERR_LENGTH 9
#define UAT_REFUSED 10

#define UAT_KIND_EMPTY 0u
#define UAT_KIND_TABLE 1u
#define UAT_KIND_BLOCK 2u
#define UAT_KIND_PAGE 3u

#define UAT_PA_BITS 40u
#define UAT_L0_SHIFT 39u
#define UAT_PAGE_SHIFT 14u
#define UAT_PAGE_SIZE (1u << UAT_PAGE_SHIFT)
#define UAT_DESCRIPTOR_FIELD_BITS 48u
#define UAT_CONTEXTS 64u
#define UAT_CONTEXT_STRIDE 16u
#define UAT_L0_ENTRIES 2u

struct uat_level {
    uint32_t level;
    uint32_t shift;
    uint32_t entries;
    uint32_t table_bytes;
    uint64_t span;
};

struct uat_flags {
    uint8_t os, uxn, pxn, ng, af, sh, ap, attr;
};

struct uat_decoded {
    uint64_t word;
    /* When has_pa is 1: table base for TABLE, output-page base for PAGE.
     * Zero is a valid base. No VA offset is added and no walk is performed.
     */
    uint64_t pa;
    uint64_t raw_output;
    uint64_t span;
    uint32_t asid;
    uint8_t kind;
    uint8_t level;
    uint8_t os, uxn, pxn, ng, af, sh, ap, attr;
    uint8_t has_pa; /* PA-field presence only, never translation success. */
    uint8_t reserved0;
};

struct uat_block_facts {
    uint64_t raw_output;
    uint64_t span;
    uint64_t virtual_offset;
};

#ifdef __cplusplus
extern "C" {
#endif

int uat_format_accepted(const char *name);
uint32_t uat_pa_bits(void);
uint32_t uat_page_size(void);
uint32_t uat_page_shift(void);
uint32_t uat_l0_shift(void);
uint32_t uat_descriptor_field_bits(void);
uint32_t uat_context_count(void);
uint32_t uat_context_stride(void);
const char *uat_evidence(void);
const char *uat_status_name(int status);

int uat_level(uint32_t index, struct uat_level *out);
int uat_table_bytes(uint32_t index, uint32_t *bytes);
int uat_fold_virtual(uint64_t va, uint64_t *folded);
int uat_virtual_index(uint32_t index, uint64_t va, uint32_t *out);
int uat_page_offset(uint64_t va, uint32_t *offset);
int uat_context_offset(uint32_t context, uint32_t *offset);

int uat_encode_empty(uint32_t level, uint64_t *word);
int uat_encode_ttbr(uint64_t pa, uint32_t asid, uint64_t *word);
int uat_encode_pte(uint32_t level, uint32_t kind, uint64_t pa,
                   const struct uat_flags *flags, uint64_t *word);
int uat_decode(uint32_t level, uint64_t word, struct uat_decoded *out);
int uat_load_word(const uint8_t *bytes, uint32_t length, uint32_t index, uint64_t *word);
int uat_store_word(uint8_t *bytes, uint32_t length, uint32_t index, uint64_t word);

/* Block facts only. pa and facts must designate disjoint caller-owned storage.
 * UAT_REFUSED publishes valid facts and leaves *pa unchanged: no translation.
 * Any UAT_ERR_* leaves both *pa and *facts unchanged.
 */
int uat_refuse_block(uint32_t level, uint64_t va, uint64_t raw_output,
                     uint64_t *pa, struct uat_block_facts *facts);
#ifdef __cplusplus
}
#endif
#endif
