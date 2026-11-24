/* Original bounded facts reader; no policy/configuration/runtime admission. */
#ifndef DARWIN_LAB_NATIVE_POLICY_READER_H
#define DARWIN_LAB_NATIVE_POLICY_READER_H
#include "../native_policy_snapshot/policy_snapshot.h"
#ifdef __cplusplus
extern "C" {
#endif
#define NPR_COMMON_PREFIX_BYTES 40u
enum npr_status { NPR_OK=0,NPR_ARGUMENT=-1,NPR_NOT_READY=-2,NPR_FORMAT=-3,
    NPR_CHANGED=-4,NPR_SOURCE=-5,NPR_FACTS=-6,NPR_CLEANUP=-7,NPR_ENVELOPE=-8 };
struct npr_workspace {
    struct nps_expected expected;
    uint8_t common_prefix[NPR_COMMON_PREFIX_BYTES],dram_bytes[16],facts[NPS_WIRE_MAX];
    struct nps_workspace decoder;
    struct nps_result staged;
};
/* Fixed /chosen; only dram-base,dram-size,darwin-lab-platform-facts.
 * Explicit precondition: trusted boot-phase serialization/quiescence makes
 * PE_state and its accessible40-byte bootArgs prefix, registry objects and all
 * borrowed OSData storage immutable/accessibly stable during this call. Retain,
 * length rechecks or repeated reads DO NOT establish that precondition.
 * Workspace must be caller-owned off stack. Expected/workspace/out disjoint.
 * Expected labels are trusted caller inputs, not self-authenticating metadata.
 * Every failure leaves out unchanged; temporary refs/views never escape. No
 * tail/full boot_args read, mapping, native owner/configure, or retry exists. */
int npr_read_native(const struct nps_expected *expected,
                    struct npr_workspace *workspace,struct nps_result *out);
#ifdef __cplusplus
}
#endif
#endif
