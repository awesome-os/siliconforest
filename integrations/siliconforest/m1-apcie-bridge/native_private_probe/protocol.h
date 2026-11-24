/* Original bounded SELF-REPORT protocol. No hardware or authentication claim. */
#ifndef BCM_PRIVATE_PROBE_PROTOCOL_H
#define BCM_PRIVATE_PROBE_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BCP_BYTES 768u
#define BCP_PROPERTY "PDBCMPrivateObservation"
#define BCP_SENTINEL UINT32_C(0x80000000)
enum bcp_result { BCP_SELF_REPORT=0,BCP_GATED=1,BCP_PENDING=2,BCP_FAILED=3 };
enum bcp_stage { BCP_GATE=0,BCP_CLAIM=1,BCP_LABELS=2,BCP_CONFIGURE=3,
 BCP_ACQUIRE=4,BCP_ZERO=5,BCP_COPY=6,BCP_PRE=7,BCP_RETIRE=8,BCP_POST=9,BCP_DONE=10 };
enum bcp_operation { BCP_LABEL_OP=0,BCP_CONFIGURE_OP,BCP_ACQUIRE_OP,BCP_ZERO_OP,
 BCP_COPY_OP,BCP_PRE_OP,BCP_RETIRE_OP,BCP_POST_OP,BCP_OPS };
enum bcp_error { BCP_MEMORY=-400,BCP_LABEL_ERROR=-401,BCP_SNAPSHOT=-402 };
enum bcp_offset { BCP_RESULT_AT=16,BCP_STAGE_AT=20,BCP_ATTEMPTS_AT=24,BCP_ERROR_AT=28,
 BCP_LABEL_CONTEXT_AT=32,BCP_SYNTHETIC_AT=36,BCP_READBACK_AT=40,BCP_EFFECT_AT=44,
 BCP_IDENTITY_AT=48,BCP_COMPONENTS_AT=264,BCP_STATUS_AT=424,BCP_CONFIG_AT=456,
 BCP_COPY_BYTES_AT=480,BCP_REQUEST_PROFILE_AT=484,BCP_PRE_AT=496,BCP_POST_AT=552,
 BCP_RESERVED_AT=608 };
/* Snapshots omit generation/sequence: phase,busy,halted,metadata_bound,
 * core_retired,call_pending,identity_records,owned_pairs,balanced_pairs,
 * resets_returned,first_error,effect_count,effect_pending,ever_resources. */
struct bcp_record { uint8_t bytes[BCP_BYTES]; };
uint32_t bcp_word(const struct bcp_record *,unsigned offset);
void bcp_put(struct bcp_record *,unsigned offset,uint32_t value);
void bcp_init(struct bcp_record *,int enabled);
/* Strict byte validation + expected nonce. 0=self-report,77=explicit unbound
 * GATED,no attempts/identity;1=all other paths. No raw data readback is possible. */
int bcp_validate(const uint8_t *,size_t,const uint8_t expected_nonce[24]);
int bcp_snapshot_ok(const struct bcp_record *,unsigned offset,int retired);
extern const uint8_t bcp_components[160];
#ifdef __cplusplus
}
#endif
#endif
