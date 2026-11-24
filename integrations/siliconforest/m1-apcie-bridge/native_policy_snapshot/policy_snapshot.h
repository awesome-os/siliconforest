/* Original bounded platform-facts decoder. No runtime producer or authority. */
#ifndef DARWIN_LAB_NATIVE_POLICY_SNAPSHOT_H
#define DARWIN_LAB_NATIVE_POLICY_SNAPSHOT_H
#include <stddef.h>
#include <stdint.h>
#include "../mcc_snapshot/mcc_snapshot.h"
#ifdef __cplusplus
extern "C" {
#endif
#define NPS_HEADER_BYTES 1280u
#define NPS_RECORD_BYTES 32u
#define NPS_RECORDS 256u
#define NPS_WIRE_MAX (NPS_HEADER_BYTES+NPS_RECORD_BYTES*NPS_RECORDS)
#define NPS_RAM_MAX 8u
#define NPS_UNION_MAX 8u
#define NPS_SOURCES 6u
#define NPS_IDENTITIES 6u
#define NPS_NONCE_BYTES 24u
#define NPS_SHA_BYTES 32u
enum nps_status { NPS_OK=0,NPS_ARGUMENT=-1,NPS_FORMAT=-2,NPS_IDENTITY=-3,
    NPS_RANGE=-4,NPS_SOURCE=-5,NPS_MCC=-6,NPS_CAPACITY=-7 };
enum nps_source { NPS_MCC_TZ=1,NPS_PMAP_IO=2,NPS_FRAMEBUFFER=3,
    NPS_DEVICE_PHYS=4,NPS_REMAP_SOURCE_ONLY=5,NPS_RETAINED_BOOT=6 };
/* Expected digests ordered KC,ramdisk,HV,baseADT,originalBA,finalnativeBA.
 * Comparison binds caller-declared identities; it cannot authenticate caller. */
struct nps_expected { uint8_t nonce[NPS_NONCE_BYTES],sha256[NPS_IDENTITIES][NPS_SHA_BYTES]; };
struct nps_source_fact { uint32_t tag,status,count; uint8_t declared_source_sha256[NPS_SHA_BYTES]; };
struct nps_record { uint32_t tag,ordinal,flags,auxiliary; struct mcp_range range; };
struct nps_result {
    uint32_t version,board_profile,page_shift,pa_bits,wire_profile,coverage;
    uint32_t ram_count,record_count,union_count;
    struct nps_expected identity;
    struct mcp_range dram,envelope,retained_boot,ram[NPS_RAM_MAX];
    struct nps_source_fact source[NPS_SOURCES];
    uint8_t raw_mcc[MCP_BYTES];
    struct mcp_result mcc;
    struct nps_record record[NPS_RECORDS];
    struct mcp_range relevant_union[NPS_UNION_MAX];
};
/* Large workspace must be caller-owned off stack. Contents unspecified on
 * failure; output unchanged. No conversion to bpl_policy or native configure. */
struct nps_workspace { struct nps_result staged; struct mcp_range sorted[NPS_RECORDS]; };
/* Requires accessible immutable input/expected and caller serialization. All
 * four object spans must be mutually disjoint. Every failure preserves output.
 * Original RAM boundaries and every source record retained. Declared source
 * digests are NOT recomputed/verified source-byte hashes; four-TZ is NOT global
 * coverage. Success proves format/consistency only, never RAM/device authority. */
int nps_decode(const uint8_t *raw,size_t bytes,const struct nps_expected *expected,
               struct nps_workspace *workspace,struct nps_result *out);
#ifdef __cplusplus
}
#endif
#endif
