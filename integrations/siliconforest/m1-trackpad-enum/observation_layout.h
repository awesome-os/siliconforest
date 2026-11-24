/* Original fixed diagnostic copy. Not a pointer/input capability or report. */
#ifndef M1TP_OBSERVATION_LAYOUT_H
#define M1TP_OBSERVATION_LAYOUT_H
#include <stdint.h>
#include <stddef.h>
#define M1TP_OBSERVATION_VERSION 1u
#define M1TP_DESCRIPTOR_CAP 512u
#define M1TP_OBSERVATION_KEY "SFM1TrackpadEnumerationV1"
enum m1tp_observation_stage {
    M1TP_OBS_BEFORE_IO=0, M1TP_OBS_INFO_REQUEST=1, M1TP_OBS_INFO_RECEIVED=2,
    M1TP_OBS_DESC_REQUEST=3, M1TP_OBS_DESC_RECEIVED=4, M1TP_OBS_COMPLETE=5
};
enum m1tp_observation_result {
    M1TP_OBS_ENUM_OK=0, M1TP_OBS_NOT_ATTEMPTED=1, M1TP_OBS_INFO_REQUEST_FAILED=-1,
    M1TP_OBS_INFO_INVALID=-2, M1TP_OBS_DESC_REQUEST_FAILED=-3, M1TP_OBS_DESC_INVALID=-4
};
/* All integers are little-endian on the ARM64 target. Header only: optional
 * name/string bytes are never copied. descriptor_bytes is stored length;
 * descriptor_payload_bytes can diagnose a rejected >512-byte response.
 * Unwritten header/descriptor bytes stay zero. One OSData copy per boot.
 */
struct m1tp_observation {
    uint32_t version;
    int32_t result;
    uint32_t stage, request_mask, metadata_payload_bytes;
    uint32_t descriptor_payload_bytes, descriptor_bytes;
    uint8_t metadata_header[16];
    uint8_t descriptor[M1TP_DESCRIPTOR_CAP];
};
#ifdef __cplusplus
#define M1TP_ASSERT(x) static_assert((x), #x)
#else
#define M1TP_ASSERT(x) _Static_assert((x), #x)
#endif
M1TP_ASSERT(sizeof(struct m1tp_observation)==556);
M1TP_ASSERT(offsetof(struct m1tp_observation,result)==4);
M1TP_ASSERT(offsetof(struct m1tp_observation,stage)==8);
M1TP_ASSERT(offsetof(struct m1tp_observation,request_mask)==12);
M1TP_ASSERT(offsetof(struct m1tp_observation,metadata_payload_bytes)==16);
M1TP_ASSERT(offsetof(struct m1tp_observation,descriptor_payload_bytes)==20);
M1TP_ASSERT(offsetof(struct m1tp_observation,descriptor_bytes)==24);
M1TP_ASSERT(offsetof(struct m1tp_observation,metadata_header)==28);
M1TP_ASSERT(offsetof(struct m1tp_observation,descriptor)==44);
#undef M1TP_ASSERT
#endif
