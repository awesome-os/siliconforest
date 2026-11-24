/* Original unshipped T2 diagnostic ABI. Not an input report or liveness lease. */
#ifndef DL_M1_TRACKPAD_CHILD_STATE_H
#define DL_M1_TRACKPAD_CHILD_STATE_H
#include <stddef.h>
#include <stdint.h>

#define DL_M1_TP_CHILD_STATE_VERSION 1u
#define DL_M1_TP_CHILD_STATE_BYTES 32u
#define DL_M1_TP_CHILD_STATE_PROPERTY "SFM1TrackpadChildStateV1"
#define DL_M1_TP_INTERFACE_PROPERTY "SFM1SPIInterfaceID"
#define DL_M1_TP_PARENT_CLASS "SFM1SPIHID"
#define DL_M1_TP_CHILD_CLASS "SFM1SPITrackpad"
#define DL_M1_TP_DESCRIPTOR_BYTES 110u
#define DL_M1_TP_DESCRIPTOR_SHA256 "4ae5bc2626bff5458ba2325e29614f1fec5e8c5a4d32c396f64d59508ec77609"
#define DL_M1_TP_REPORT02_BYTES 8u
#define DL_M1_TP_REPORT3F_BYTES 17u
#define DL_M1_TP_REPORT44_BYTES 1752u

/* Native little endian on this ARM64 candidate. Copies are non-atomic across
 * registry operations. A ready tuple does not prove a later report or input. */
struct dl_m1_trackpad_child_state_v1 {
    uint32_t version;
    uint32_t byte_size;
    uint32_t committed;
    uint32_t route_ready;
    uint32_t faulted;
    uint32_t stop_requested;
    int32_t last_error;
    uint32_t reserved;
};

#ifdef __cplusplus
#define DL_M1_TP_CHILD_ASSERT(c) static_assert((c), #c)
#else
#define DL_M1_TP_CHILD_ASSERT(c) _Static_assert((c), #c)
#endif
DL_M1_TP_CHILD_ASSERT(sizeof(struct dl_m1_trackpad_child_state_v1) == 32);
DL_M1_TP_CHILD_ASSERT(offsetof(struct dl_m1_trackpad_child_state_v1, version) == 0);
DL_M1_TP_CHILD_ASSERT(offsetof(struct dl_m1_trackpad_child_state_v1, byte_size) == 4);
DL_M1_TP_CHILD_ASSERT(offsetof(struct dl_m1_trackpad_child_state_v1, committed) == 8);
DL_M1_TP_CHILD_ASSERT(offsetof(struct dl_m1_trackpad_child_state_v1, route_ready) == 12);
DL_M1_TP_CHILD_ASSERT(offsetof(struct dl_m1_trackpad_child_state_v1, faulted) == 16);
DL_M1_TP_CHILD_ASSERT(offsetof(struct dl_m1_trackpad_child_state_v1, stop_requested) == 20);
DL_M1_TP_CHILD_ASSERT(offsetof(struct dl_m1_trackpad_child_state_v1, last_error) == 24);
DL_M1_TP_CHILD_ASSERT(offsetof(struct dl_m1_trackpad_child_state_v1, reserved) == 28);
#undef DL_M1_TP_CHILD_ASSERT
#endif
